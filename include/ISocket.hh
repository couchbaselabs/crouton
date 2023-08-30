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
        virtual void bind(std::string const& address, uint16_t port, bool withTLS) {
            assert(!_binding);
            _binding.reset(new binding{address, port, withTLS});
        }

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] virtual Future<void> open() =0;

        /// Equivalent to bind + open.
        [[nodiscard]] virtual Future<void> connect(std::string const& address, uint16_t port, bool withTLS) {
            bind(address, port, withTLS);
            return open();
        }

        /// Sets the TCP nodelay option.
        virtual void setNoDelay(bool) =0;

        /// Enables TCP keep-alive with the given ping interval.
        virtual void keepAlive(unsigned intervalSecs) =0;

        virtual ~ISocket() = default;

    protected:
        struct binding {
            std::string address;
            uint16_t port;
            bool withTLS;
        };

        std::unique_ptr<binding> _binding;
    };

}
