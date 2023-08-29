//
// TCPSocket.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include "Stream.hh"

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
