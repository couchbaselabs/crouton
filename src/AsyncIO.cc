//
// AsyncIO.cc
//
// 
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "AsyncIO.hh"
#include "Defer.hh"
#include "UVAwait.hh"
#include <unistd.h>
#include <iostream>

namespace snej::coro::uv {
    using namespace std;


#pragma mark - FILE STREAM:


    Future<bool> FileStream::open(std::string const& path, Flags flags, int mode) {
        assert(!isOpen());
        uv::fs_request req;
        uv::check(uv_fs_open(uv_default_loop(), &req, path.c_str(), flags, mode, req.callback));
        co_await req;

        uv::check(req.result);
        _fd = int(req.result);
        co_return true;
    }


    Future<int64_t> FileStream::read(size_t len, void* dst) {
        assert(isOpen());
        uv::fs_request req;
        uv_buf_t buf = {.len = len, .base = (char*)dst};
        uv::check(uv_fs_read(uv_default_loop(), &req, _fd, &buf, 1, -1, req.callback));
        co_await req;

        uv::check(req.result);
        co_return req.result;
    }


    void FileStream::close() {
        if (isOpen()) {
            // Close synchronously, for simplicity
            uv_fs_t closeReq;
            uv_fs_close(uv_default_loop(), &closeReq, _fd, nullptr);
            _fd = -1;
        }
    }


#pragma mark - DNS LOOKUP:


    class getaddrinfo_request : public uv::RequestWithStatus<uv_getaddrinfo_s> {
    public:
        static void callback(uv_getaddrinfo_s *req, int status, struct addrinfo *res) {
            auto self = static_cast<getaddrinfo_request*>(req);
            self->info = res;
            self->callbackWithStatus(req, status);
        }

        struct addrinfo* info = nullptr;
    };


    AddrInfo::~AddrInfo() {
        uv_freeaddrinfo(_info);
    }


    Future<bool> AddrInfo::lookup(string hostName, uint16_t port) {
        uv_freeaddrinfo(_info);
        _info = nullptr;

        struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
        };

        const char* service = nullptr;
        char portStr[10];
        if (port != 0) {
            snprintf(portStr, 10, "%u", port);
            service = portStr;  // This causes the 'port' fields of the addrinfos to be filled in
        }

        getaddrinfo_request req;
        uv::check(uv_getaddrinfo(uv_default_loop(), &req, req.callback,
                                 hostName.c_str(), service, &hints));
        uv::check( co_await req );

        _info = req.info;
        co_return true;
    }


    struct ::sockaddr const* AddrInfo::primaryAddress(int ipv) const {
        assert(_info);
        int af = ipv;
        switch (ipv) {
            case 4: af = AF_INET; break;
            case 6: af = AF_INET6; break;
        }

        for (auto i = _info; i; i = i->ai_next) {
            if (i->ai_socktype == SOCK_STREAM && i->ai_protocol == IPPROTO_TCP && i->ai_family == af)
                return i->ai_addr;
        }
        return nullptr;
    }

    struct ::sockaddr const* AddrInfo::primaryAddress() const {
        auto addr = primaryAddress(4);
        return addr ? addr : primaryAddress(6);
    }

    std::string AddrInfo::primaryAddressString() const {
        char buf[100];
        auto *addr = primaryAddress();
        if (!addr)
            return "";
        int err;
        if (addr->sa_family == PF_INET)
            err = uv_ip4_name((struct sockaddr_in*)addr, buf, sizeof(buf) - 1);
        else
            err = uv_ip6_name((struct sockaddr_in6*)addr, buf, sizeof(buf) - 1);
        return err ? "" : buf;
    }



#pragma mark - TCP SOCKET:


    struct Blocker {

        void resume() {
            assert(_suspension);
            _suspension->wakeUp();
            _suspension = nullptr;
        }

        bool await_ready()      {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coro) noexcept {
            assert(!_suspension);
            _suspension = Scheduler::current().suspend(coro);
            return Scheduler::current().next();
        }
        uv_buf_t await_resume()  {return _buf;}

        Suspension* _suspension = nullptr;
        uv_buf_t _buf = { };
        ssize_t _nread = 0;
    };


    TCPSocket::TCPSocket()
    :_tcpHandle(make_unique<uv_tcp_s>())
    {
        uv_tcp_init(uv_default_loop(), _tcpHandle.get());
    }


    TCPSocket::~TCPSocket() {
        close();
    }


    Future<bool> TCPSocket::connect(std::string const& address, uint16_t port) {
        assert(!_socket);

        sockaddr addr;
        int status = uv_ip4_addr(address.c_str(), port, (sockaddr_in*)&addr);
        if (status < 0) {
            AddrInfo ai;
            co_await ai.lookup(address, port);
            if (sockaddr const* socka = ai.primaryAddress())
                addr = *socka;
            else
                throw std::runtime_error("no primary address?!");
        }

        uv::connect_request req;
        uv::check(uv_tcp_connect(&req, _tcpHandle.get(), &addr, req.callbackWithStatus));
        uv::check( co_await req );

        _socket = req.handle;
        co_return true;
    }


    Generator<std::string>& TCPSocket::reader() {
        if (!_reader)
            _reader.emplace(_createReader());
        return *_reader;
    }


    Generator<string> TCPSocket::_createReader() {
        assert(isOpen());

        Blocker blocker;
        _socket->data = &blocker;

        //TODO: Improve memory management
        auto allocCallback = [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
            buf->base = (char*)::malloc(suggested_size);
            buf->len = suggested_size;
        };

        auto readCallback = [](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
            auto blocker = (Blocker*)stream->data;
            blocker->_nread = nread;
            blocker->_buf = *buf;
            blocker->resume();
        };

        uv::check(uv_read_start(_socket, allocCallback, readCallback));

        DEFER { uv_read_stop(_socket); };

        do {
            co_await blocker;

            if (blocker._nread == UV_EOF)
                break;
            uv::check(blocker._nread);
            if (blocker._nread > 0)
                co_yield string(blocker._buf.base, blocker._nread);
            ::free(blocker._buf.base);
        } while (blocker._nread > 0);
    }


    Future<bool> TCPSocket::write(std::string str) {
        assert(isOpen());

        uv::write_request req;
        uv_buf_t buf = {.base = str.data(), .len = str.size()};
        uv::check(uv_write(&req, _socket, &buf, 1, req.callbackWithStatus));
        uv::check( co_await req );

        co_return true;
    }


    Future<bool> TCPSocket::shutdown() {
        assert(isOpen());

        RequestWithStatus<uv_shutdown_t> req;
        check( uv_shutdown(&req, _socket, req.callbackWithStatus) );
        check( co_await req );
        co_return true;
    }


    void TCPSocket::close() {
        if (_socket) {
            uv_close((uv_handle_t*)_socket, nullptr);
        }
    }

}
