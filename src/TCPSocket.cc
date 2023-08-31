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

namespace crouton {
    using namespace std;


    /** Stream wrapper for a uv_tcp_t handle. */
    struct tcp_stream_wrapper : public uv_stream_wrapper {
        explicit tcp_stream_wrapper(uv_tcp_t *s) :uv_stream_wrapper((uv_stream_t*)s) { }
        uv_tcp_t* tcpHandle()                   {return (uv_tcp_t*)_stream;}
        virtual int setNoDelay(bool e)          {return uv_tcp_nodelay(tcpHandle(), e);}
        virtual int keepAlive(unsigned i)       {return uv_tcp_keepalive(tcpHandle(), (i > 0), i);}
    };


    TCPSocket::TCPSocket() = default;


    void TCPSocket::acceptFrom(uv_tcp_s* server) {
        auto tcpHandle = new uv_tcp_t;
        uv_tcp_init(curLoop(), tcpHandle);
        check(uv_accept((uv_stream_t*)server, (uv_stream_t*)tcpHandle),
              "accepting client connection");
        opened(make_unique<tcp_stream_wrapper>(tcpHandle));
    }


    Future<void> TCPSocket::open() {
        assert(!isOpen());
        assert(_binding);
        std::unique_ptr<stream_wrapper> stream;
        connect_request req;
        int err;

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

        stream->setNoDelay(_binding->noDelay);
        stream->keepAlive(_binding->keepAlive);
        _binding = nullptr;

        check(err, "opening connection");
        check( AWAIT req, "opening connection" );

        opened(std::move(stream));
        RETURN;
    }

}
