//
// TCPServer.cc
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

#include "io/TCPServer.hh"
#include "io/TCPSocket.hh"
#include "UVInternal.hh"
#include "util/Logging.hh"

namespace crouton::io {
    using namespace std;
    using namespace crouton::io::uv;


    TCPServer::TCPServer(uint16_t port)
    :_tcpHandle(new uv_tcp_s)
    {
        uv_tcp_init(curLoop(), _tcpHandle);
        _tcpHandle->data = this;
        sockaddr_in addr = {};
        uv_ip4_addr("0.0.0.0", port, &addr);
        check(uv_tcp_bind(_tcpHandle, (sockaddr*)&addr, 0), "initializing server");
    }

    TCPServer::~TCPServer() {
        close();
    }


    void TCPServer::listen(std::function<void(std::shared_ptr<TCPSocket>)> acceptor) {
        _acceptor = std::move(acceptor);
        check(uv_listen((uv_stream_t*)_tcpHandle, 2, [](uv_stream_t *server, int status) noexcept {
            try {
                ((TCPServer*)server->data)->accept(status);
            } catch (...) {
                LNet->error("Caught unexpected exception in TCPServer::accept");
            }
        }), "starting server");
    }


    void TCPServer::close() {
        closeHandle(_tcpHandle);
    }


    void TCPServer::accept(int status) {
        if (status < 0) {
            LNet->error("TCPServer::listen failed: error {} {}", status, uv_strerror(status));
            //TODO: Notify the app somehow
        } else {
            auto clientHandle = new uv_tcp_t;
            uv_tcp_init(curLoop(), clientHandle);
            check(uv_accept((uv_stream_t*)_tcpHandle, (uv_stream_t*)clientHandle),
                  "accepting client connection");
            auto client = make_shared<TCPSocket>();
            client->accept(clientHandle);
            _acceptor(std::move(client));
        }
    }

}
