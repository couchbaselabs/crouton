//
// TCPSocket.cc
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
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

#include "TCPSocket.hh"
#include "AddrInfo.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include "stream_wrapper.hh"
#include <mutex>
#include <unistd.h>
#include <iostream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#endif

#include "tlsuv.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace crouton {
    using namespace std;


#pragma mark - STREAM WRAPPERS:


    static void initTlsuv() {
        static once_flag sOnce;
        std::call_once(sOnce, [] {
            tlsuv_set_debug(3, // INFO
                            [](int level, const char *file, unsigned int line, const char *msg) {
                if (level <= 1) // fatal
                    throw std::runtime_error("TLSUV: "s + msg);
                std::cerr << "TLSUV: " << msg << std::endl;
            });
        });
    }


    /** Wrapper around a uv_tcp_t handle. */
    struct tcp_stream_wrapper : public uv_stream_wrapper {
        explicit tcp_stream_wrapper(uv_tcp_t *socket)   :uv_stream_wrapper((uv_stream_t*)socket) { }
        uv_tcp_t* tcpHandle()                           {return (uv_tcp_t*)_stream;}
        virtual int setNoDelay(bool e)                  {return uv_tcp_nodelay(tcpHandle(), e);}
        virtual int keepAlive(unsigned i)       {return uv_tcp_keepalive(tcpHandle(), (i > 0), i);}
    };


    /** Wrapper around a tlsuv_stream_t handle. */
    struct tlsuv_stream_wrapper final : public stream_wrapper {

        explicit tlsuv_stream_wrapper(tlsuv_stream_t *s)    :_stream(s) {_stream->data = this;}

        ~tlsuv_stream_wrapper() {
            if (_stream) {
                _stream->data = nullptr;
                tlsuv_stream_close(_stream, [](uv_handle_t* h) noexcept {
                    tlsuv_stream_free((tlsuv_stream_t*)h);
                    delete (tlsuv_stream_t*)h;
                });
                _stream = nullptr;
            }
        }

        virtual int setNoDelay(bool enable) override {
            return tlsuv_stream_nodelay(_stream, enable);
        }

        virtual int keepAlive(unsigned intervalSecs) override {
            return tlsuv_stream_keepalive(_stream, (intervalSecs > 0), intervalSecs);
        }

        int read_start() override {
            auto alloc = [](uv_handle_t* fakeH, size_t suggested, uv_buf_t* uvbuf) {
                auto h = (tlsuv_stream_t*)fakeH;
                auto stream = (tlsuv_stream_wrapper*)h->data;
                stream->alloc(suggested, uvbuf);
            };
            auto read = [](uv_stream_t* fakeH, ssize_t nread, const uv_buf_t* uvbuf) {
                auto h = (tlsuv_stream_t*)fakeH;
                auto stream = (tlsuv_stream_wrapper*)h->data;
                stream->read(nread, uvbuf);
            };
            return tlsuv_stream_read(_stream, alloc, read);
        }

        int write(uv_write_t *req, const uv_buf_t bufs[], unsigned nbufs, uv_write_cb cb) override {
            assert(nbufs == 1); //FIXME
            return tlsuv_stream_write(req, _stream, (uv_buf_t*)&bufs[0], cb);
        }

        int try_write(const uv_buf_t bufs[], unsigned nbufs) override {
            return UV_ENOTSUP; //FIXME
        }

        bool is_readable() override {return true;} //FIXME
        bool is_writable() override {return true;} //FIXME

        int shutdown(uv_shutdown_t* req, uv_shutdown_cb cb) override {
            return UV_ENOTSUP; //FIXME
        }

        tlsuv_stream_t* _stream;
    };


#pragma mark - TCP SOCKET:


    TCPSocket::TCPSocket() = default;


    void TCPSocket::acceptFrom(uv_tcp_s* server) {
        auto tcpHandle = new uv_tcp_t;
        uv_tcp_init(curLoop(), tcpHandle);
        check(uv_accept((uv_stream_t*)server, (uv_stream_t*)tcpHandle),
              "accepting client connection");
        opened(make_unique<tcp_stream_wrapper>(tcpHandle));
    }


    Future<void> TCPSocket::connect(std::string const& address, uint16_t port, bool withTLS) {
        bind(address, port, withTLS);
        return open();
    }


    void TCPSocket::bind(std::string const& address, uint16_t port, bool withTLS) {
        assert(!isOpen());
        assert(!_binding);
        _binding.reset(new binding{address, port, withTLS});
    }


    Future<void> TCPSocket::open() {
        assert(!isOpen());
        assert(_binding);
        std::unique_ptr<stream_wrapper> stream;
        connect_request req;
        int err;
        if (_binding->withTLS) {
            initTlsuv();
            tlsuv_stream_t *tlsHandle = new tlsuv_stream_t;
            tlsuv_stream_init(curLoop(), tlsHandle, NULL);
            stream = make_unique<tlsuv_stream_wrapper>(tlsHandle);
            err = tlsuv_stream_connect(&req, tlsHandle, _binding->address.c_str(), _binding->port,
                                       req.callbackWithStatus);

        } else {
            // Resolve the address/hostname:
            sockaddr addr;
            int status = uv_ip4_addr(_binding->address.c_str(), _binding->port, (sockaddr_in*)&addr);
            if (status < 0) {
                AddrInfo ai = AWAIT AddrInfo::lookup(_binding->address, _binding->port);
                addr = ai.primaryAddress();
            }

            auto tcpHandle = new uv_tcp_t;
            uv_tcp_init(curLoop(), tcpHandle);
            stream = make_unique<tcp_stream_wrapper>(tcpHandle);
            err = uv_tcp_connect(&req, tcpHandle, &addr, 
                                 req.callbackWithStatus);
        }
        _binding = nullptr;

        check(err, "opening connection");
        check( AWAIT req, "opening connection" );

        opened(std::move(stream));
        RETURN;
    }


    void TCPSocket::setNoDelay(bool enable) {
        _stream->setNoDelay(enable);
    }

    void TCPSocket::keepAlive(unsigned intervalSecs) {
        _stream->keepAlive(intervalSecs);
    }

}
