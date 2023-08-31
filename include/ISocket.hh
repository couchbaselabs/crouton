//
// ISocket.hh
//
// 
//

#pragma once
#include "Future.hh"
#include <memory>
#include <string>
#include <cassert>

namespace crouton {

    /** Abstract interface for opening a network connection. */
    class ISocket {
    public:
        /// Specifies the address and port to connect to, and whether to use TLS.
        virtual void bind(std::string const& address, uint16_t port) {
            assert(!_binding);
            _binding.reset(new binding{address, port});
        }

        /// Sets the TCP nodelay option.
        virtual void setNoDelay(bool b)                 {_binding->noDelay = b;}

        /// Enables TCP keep-alive with the given ping interval.
        virtual void keepAlive(unsigned intervalSecs)   {_binding->keepAlive = intervalSecs;}

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] virtual Future<void> open() =0;

        /// Equivalent to bind + open.
        [[nodiscard]] virtual Future<void> connect(std::string const& address, uint16_t port) {
            bind(address, port);
            return open();
        }

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
