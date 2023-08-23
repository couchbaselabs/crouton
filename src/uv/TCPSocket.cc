//
// TCPSocket.cc
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

#include "TCPSocket.hh"
#include "AddrInfo.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include "uv_stream_wrapper.hh"
#include <mutex>
#include <unistd.h>
#include <iostream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wnewline-eof"
#endif

#include "tlsuv.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace snej::coro::uv {
    using namespace std;


    static void initUVTLS() {
        static once_flag sOnce;
        std::call_once(sOnce, [] {
            tlsuv_set_debug(4, [](int level, const char *file, unsigned int line, const char *msg) {
                if (level <= 1) // fatal
                    throw std::runtime_error("TLSUV: "s + msg);
                std::cerr << "TLSUV: " << msg << std::endl;
            });
        });
    }

    /** Wrapper around a tlsuv_stream_t. */
    struct tlsuv_stream_wrapper final : public stream_wrapper {

        explicit tlsuv_stream_wrapper(tlsuv_stream_t *stream) 
        :_stream(stream)
        {
            _stream->data = this;
        }

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

        int read_start(ReadCallback cb) override {
            int err = stream_wrapper::read_start(cb);
            if (err == 0 && _readCallback) {
                auto alloc = [](uv_handle_t* fakeH, size_t, uv_buf_t* uvbuf) {
                    auto h = (tlsuv_stream_t*)fakeH;
                    auto stream = (tlsuv_stream_wrapper*)h->data;
                    stream->alloc(uvbuf);
                };
                auto read = [](uv_stream_t* fakeH, ssize_t nread, const uv_buf_t* uvbuf) {
                    auto h = (tlsuv_stream_t*)fakeH;
                    auto stream = (tlsuv_stream_wrapper*)h->data;
                    stream->read(nread, uvbuf);
                };
                err = tlsuv_stream_read(_stream, alloc, read);
            }
            return err;
        }

        int write(uv_write_t *req, const uv_buf_t bufs[], unsigned nbufs, uv_write_cb cb) override {
            assert(nbufs == 1); //FIXME
            return tlsuv_stream_write(req, _stream, (uv_buf_t*)&bufs[0], cb);
        }

        int try_write(const uv_buf_t bufs[], unsigned nbufs) override {
            return 0; //FIXME
        }

        bool is_readable() override {return true;} //FIXME
        bool is_writable() override {return true;} //FIXME

        int shutdown(uv_shutdown_t* req, uv_shutdown_cb cb) override {
            return 0; //FIXME
        }

        tlsuv_stream_t* _stream;
    };



    TCPSocket::TCPSocket()
    :_tcpHandle(new uv_tcp_s) // will be deleted by close's call to closeHandle
    {
        uv_tcp_init(curLoop(), _tcpHandle);
    }


    void TCPSocket::acceptFrom(uv_tcp_s* server) {
        check(uv_accept((uv_stream_t*)server, (uv_stream_t*)_tcpHandle),
              "accepting client connection");
        opened(make_unique<uv_stream_wrapper>(_tcpHandle));
    }


    Future<void> TCPSocket::connect(std::string const& address, uint16_t port, bool withTLS) {
        assert(!isOpen());

        if (withTLS) {
            initUVTLS();
            closeHandle(_tcpHandle);
            tlsuv_stream_t *tlsStream = new tlsuv_stream_t;
            tlsuv_stream_init(curLoop(), tlsStream, NULL);

            connect_request req;
            check(tlsuv_stream_connect(&req, tlsStream, address.c_str(), port,
                                       req.callbackWithStatus),
                  "opening TLS connection");
            check( AWAIT req, "opening TLS connection" );

            opened(make_unique<tlsuv_stream_wrapper>(tlsStream));

        } else {
            sockaddr addr;
            int status = uv_ip4_addr(address.c_str(), port, (sockaddr_in*)&addr);
            if (status < 0) {
                AddrInfo ai;
                AWAIT ai.lookup(address, port);
                if (sockaddr const* socka = ai.primaryAddress())
                    addr = *socka;
                else
                    throw std::runtime_error("no primary address?!");
            }

            connect_request req;
            check(uv_tcp_connect(&req, _tcpHandle, &addr, req.callbackWithStatus),
                  "opening connection");
            check( AWAIT req, "opening connection" );

            opened(make_unique<uv_stream_wrapper>(req.handle));
        }
        RETURN;
    }


    void TCPSocket::setNoDelay(bool enable) {
        uv_tcp_nodelay(_tcpHandle, enable);
    }

    void TCPSocket::keepAlive(unsigned intervalSecs) {
        uv_tcp_keepalive(_tcpHandle, (intervalSecs > 0), intervalSecs);
    }

}
