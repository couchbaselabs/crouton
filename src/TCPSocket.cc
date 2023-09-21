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
#include "UVInternal.hh"

namespace crouton {
    using namespace std;


    TCPSocket::TCPSocket() = default;


    Future<void> TCPSocket::open() {
        assert(!isOpen());
        assert(_binding);
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
        uv_tcp_nodelay(tcpHandle, _binding->noDelay);
        uv_tcp_keepalive(tcpHandle, (_binding->keepAlive > 0), _binding->keepAlive);
        _binding.reset();

        connect_request req("opening connection");
        err = uv_tcp_connect(&req, tcpHandle, &addr,
                             req.callbackWithStatus);
        if (err < 0) {
            closeHandle(tcpHandle);
            check(err, "opening connection");
        }

        AWAIT req;

        opened((uv_stream_t*)tcpHandle);
        RETURN noerror;
    }

}
