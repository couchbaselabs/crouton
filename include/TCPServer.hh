//
// TCPServer.hh
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

#pragma once
#include "UVBase.hh"
#include <functional>
#include <memory>

struct uv_tcp_s;

namespace crouton {
    class TCPSocket;

    class TCPServer {
    public:
        TCPServer(uint16_t port);
        ~TCPServer();

        using Acceptor = std::function<void(std::shared_ptr<TCPSocket>)>;

        void listen(Acceptor);

        void close();

    private:
        void accept(int status);
        
        uv_tcp_s*       _tcpHandle;         // Handle for TCP operations
        Acceptor        _acceptor;
    };

}
