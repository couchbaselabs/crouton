//
// ISocket.hh
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
#include "Future.hh"
#include "Task.hh"
#include <memory>
#include <string>
#include <cassert>

namespace crouton {
    class IStream;

    /** Abstract interface for opening a network connection. */
    class ISocket {
    public:

        /// Creates a new ISocket instance of a default subclass.
        static std::unique_ptr<ISocket> newSocket(bool useTLS);

        /// Specifies the address and port to connect to, and whether to use TLS.
        virtual void bind(std::string const& address, uint16_t port) {
            assert(!_binding);
            _binding.reset(new binding{address, port});
        }

        /// Sets the TCP nodelay option. Call this after `bind`.
        virtual void setNoDelay(bool b)                 {_binding->noDelay = b;}

        /// Enables TCP keep-alive with the given ping interval. Call this after `bind`.
        virtual void keepAlive(unsigned intervalSecs)   {_binding->keepAlive = intervalSecs;}

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] virtual Future<void> open() =0;

        /// Equivalent to bind + open.
        [[nodiscard]] virtual Future<void> connect(std::string const& address, uint16_t port) {
            bind(address, port);
            return open();
        }

        /// True if the socket is open/connected.
        virtual bool isOpen() const =0;

        /// The socket's data stream.
        virtual IStream& stream() =0;

        [[nodiscard]] virtual Future<void> close() =0;

        /// Convenience function that calls `close`, waits for completion, then deletes.
        static Task closeAndFree(std::unique_ptr<ISocket>);

        virtual ~ISocket() = default;

    protected:
        struct binding {
            std::string address;
            uint16_t port;
            bool noDelay = false;
            unsigned keepAlive = 0;
        };

        std::unique_ptr<binding> _binding;
    };

}
