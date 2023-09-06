//
// TCPSocket.hh
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
#include "ISocket.hh"
#include "Stream.hh"
#include <memory>

struct uv_tcp_s;

namespace crouton {

    /** A TCP socket. (For TLS connections, use TLSSocket or NWConnection.) */
    class TCPSocket : public Stream, public ISocket {
    public:
        TCPSocket();

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] Future<void> open() override;

        bool isOpen() const override            {return Stream::isOpen();}
        IStream& stream() override              {return *this;}
        IStream const& stream() const override  {return *this;}
        [[nodiscard]] Future<void> close() override  {return Stream::close();}

    private:
        friend class TCPServer;

        struct binding {
            std::string address;
            uint16_t port;
            bool withTLS;
        };

        virtual void acceptFrom(uv_tcp_s* server);
    };
}
