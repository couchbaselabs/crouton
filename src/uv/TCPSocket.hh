//
// TCPSocket.hh
//
// 
//

#pragma once
#include "Stream.hh"

struct uv_tcp_s;
struct tlsuv_stream_s;

namespace snej::coro::uv {

    /** A TCP socket. */
    class TCPSocket : public Stream {
    public:
        TCPSocket();

        /// Connects to an address/port. The address may be a hostname or dotted-quad IPv4 address.
        [[nodiscard]] Future<void> connect(std::string const& address, uint16_t port, bool withTLS);

        /// Sets the TCP nodelay option.
        void setNoDelay(bool);

        /// Enables TCP keep-alive with the given ping interval.
        void keepAlive(unsigned intervalSecs);

    private:
        friend class TCPServer;
        
        virtual void acceptFrom(uv_tcp_s* server);
    };
}
