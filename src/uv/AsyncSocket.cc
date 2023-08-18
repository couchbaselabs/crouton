//
// AsyncSocket.cc
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

#include "AsyncSocket.hh"
#include "AddrInfo.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include <unistd.h>
#include <iostream>

namespace snej::coro::uv {
    using namespace std;


    TCPSocket::TCPSocket()
    :_tcpHandle(new uv_tcp_s)
    {
        uv_tcp_init(curLoop(), _tcpHandle);
    }


    TCPSocket::~TCPSocket() {
        close();
    }


    Future<void> TCPSocket::connect(std::string const& address, uint16_t port) {
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

        connect_request req;
        check(uv_tcp_connect(&req, _tcpHandle, &addr, req.callbackWithStatus),
              "opening connection");
        check( co_await req, "opening connection" );

        _socket = req.handle;   // note: this is the same address as _tcpHandle
        co_return;
    }


    void TCPSocket::setNoDelay(bool enable) {
        uv_tcp_nodelay(_tcpHandle, enable);
    }

    void TCPSocket::keepAlive(unsigned intervalSecs) {
        uv_tcp_keepalive(_tcpHandle, (intervalSecs > 0), intervalSecs);
    }


    Future<void> TCPSocket::shutdown() {
        assert(isOpen());

        RequestWithStatus<uv_shutdown_t> req;
        check( uv_shutdown(&req, _socket, req.callbackWithStatus), "closing connection");
        check( co_await req, "closing connection" );
        co_return;
    }


    void TCPSocket::close() {
        freeInputBuf();
        if (_spareInputBuf.base) {
            ::free(_spareInputBuf.base);
            _spareInputBuf = {};
        }
        _socket = nullptr;
        closeHandle(_tcpHandle);
    }


#pragma mark - READING:


    bool TCPSocket::isReadable() const {
        return _socket && (_inputOff < _inputBuf.len || uv_is_readable(_socket));
    }

    void TCPSocket::freeInputBuf() {
        ::free(_inputBuf.base);
        _inputBuf.base = nullptr;
    }


    Future<void> TCPSocket::readExactly(size_t len, void* dst) {
        int64_t bytesRead = co_await read(len, dst);
        if (bytesRead < len)
            check(int(UV_EOF), "reading from the network");
        co_return;
    }


    Future<string> TCPSocket::read(size_t maxLen) {
        NotReentrant nr(_readBusy);
        static constexpr size_t kGrowSize = 32768;
        string data;
        size_t len = 0;
        while (len < maxLen) {
            size_t n = std::min(kGrowSize, maxLen - len);
            data.resize(len + n);
            size_t bytesRead = co_await _read(n, &data[len]);

            if (bytesRead < n) {
                data.resize(len + bytesRead);
                break;
            }
            len += bytesRead;
        }
        co_return data;
    }


    Future<int64_t> TCPSocket::read(size_t maxLen, void* dst) {
        NotReentrant nr(_readBusy);
        return _read(maxLen, dst);
    }

    Future<int64_t> TCPSocket::_read(size_t maxLen, void* dst) {
        size_t bytesRead = 0;
        while (bytesRead < maxLen) {
            WriteBuf bytes = co_await _readNoCopy(maxLen - bytesRead);
            if (bytes.len == 0)
                break;
            ::memcpy((char*)dst + bytesRead, bytes.base, bytes.len);
            bytesRead += bytes.len;
        }
        co_return bytesRead;
    }


    Future<WriteBuf> TCPSocket::readNoCopy(size_t maxLen) {
        NotReentrant nr(_readBusy);
        return _readNoCopy(maxLen);
    }


    Future<WriteBuf> TCPSocket::_readNoCopy(size_t maxLen) {
        assert(isOpen());
        if (_inputOff == _inputBuf.len) {
            // Read buffer exhausted: recycle or free it
            if (_spareInputBuf.base == nullptr) {
                _spareInputBuf = _inputBuf;
                _inputBuf = {};
            } else {
                freeInputBuf();
            }
        }

        if (!_inputBuf.base) {
            // Reload the input buffer from the socket:
            _inputBuf = co_await readBuf();

            _inputOff = 0;
            if (_inputBuf.len == 0)
                co_return {};  // Reached EOF
        }

        // Advance _inputOff and return the pointer:
        size_t n = std::min(maxLen, _inputBuf.len - _inputOff);
        WriteBuf result{.base = (char*)_inputBuf.base + _inputOff, .len = n};
        _inputOff += n;
        co_return result;
    }


    /// The base read method. Reads once from the uv_stream and returns the result as a
    /// uv_buf_t. Caller must free it.
    Future<TCPSocket::BufWithCapacity> TCPSocket::readBuf() {
        assert(isOpen());

        struct state_t {
            Blocker<BufWithCapacity> blocker;
            TCPSocket* self;
        };
        state_t state;
        state.self = this;
        _socket->data = &state;

        auto allocCallback = [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
            auto state = (state_t*)handle->data;
            if (auto self = state->self; self->_spareInputBuf.base) {
                buf->base = (char*)self->_spareInputBuf.base;
                buf->len = self->_spareInputBuf.capacity;
                self->_spareInputBuf = {};
            } else {
                buf->base = (char*)::malloc(suggested_size);
                buf->len = suggested_size;
            }
        };

        auto readCallback = [](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
            uv_read_stop(stream); // We only want this called once!
            auto state = (state_t*)stream->data;
            if (nread >= 0)
                state->blocker.resume(BufWithCapacity{
                    {.base = buf->base, .len = size_t(nread)},
                    .capacity = buf->len});
            else if (nread == UV_EOF || nread == UV_EINVAL)  // at end, or socket is closing
                state->blocker.resume(BufWithCapacity{});
            else
                state->blocker.fail(int(nread), "reading from the network");
        };

        check(uv_read_start(_socket, allocCallback, readCallback), "reading from the network");

        BufWithCapacity result = co_await state.blocker;

        _socket->data = nullptr;
        co_return result;
    }


#pragma mark - WRITING:


    bool TCPSocket::isWritable() const {
        return _socket && uv_is_writable(_socket);
    }

    Future<void> TCPSocket::write(const WriteBuf buffers[], size_t nBuffers) {
        NotReentrant nr(_writeBusy);
        assert(isOpen());
        write_request req;
        check(uv_write(&req, _socket, (uv_buf_t*)buffers, unsigned(nBuffers),
                           req.callbackWithStatus),
              "sending to the network");
        check( co_await req, "sending to the network");

        co_return;
    }

    Future<void> TCPSocket::write(std::initializer_list<WriteBuf> buffers) {
        return write(buffers.begin(), buffers.size());
    }

    Future<void> TCPSocket::write(size_t len, const void* src) {
        WriteBuf buf{src, len};
        return write(&buf, 1);
    }

    Future<void> TCPSocket::write(std::string str) {
        // Use co_await to ensure `str` stays in scope until the write completes.
        co_await write(str.size(), str.data());
        co_return;
    }


    size_t TCPSocket::tryWrite(WriteBuf buf) {
        int result = uv_try_write(_socket, (uv_buf_t*)&buf, 1);
        if (result == UV_EAGAIN)
            return 0;
        else
            check(result, "sending to the network");
        return size_t(result);
    }
}
