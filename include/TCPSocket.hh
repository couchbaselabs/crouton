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
#include "Stream.hh"
#include <memory>

struct uv_tcp_s;
struct tlsuv_stream_s;

namespace crouton {

    /** A TCP socket. */
    class TCPSocket : public Stream {
    public:
        TCPSocket();

        /// Specifies the address and port to connect to, and whether to use TLS.
        void bind(std::string const& address, uint16_t port, bool withTLS);

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] virtual Future<void> open() override;

        /// Equivalent to bind + open.
        [[nodiscard]] Future<void> connect(std::string const& address, uint16_t port, bool withTLS);

        /// Sets the TCP nodelay option.
        void setNoDelay(bool);

        /// Enables TCP keep-alive with the given ping interval.
        void keepAlive(unsigned intervalSecs);

    private:
        friend class TCPServer;

        struct binding {
            std::string address;
            uint16_t port;
            bool withTLS;
        };

        virtual void acceptFrom(uv_tcp_s* server);

        std::unique_ptr<binding> _binding;
    };
}
