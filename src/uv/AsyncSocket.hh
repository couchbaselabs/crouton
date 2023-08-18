//
// AsyncSocket.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "Generator.hh"
#include "UVBase.hh"
#include <string>

struct addrinfo;
struct sockaddr;
struct uv_stream_s;
struct uv_tcp_s;

namespace snej::coro::uv {

    /** A DNS lookup. */
    class AddrInfo {
    public:
        AddrInfo() = default;
        ~AddrInfo();

        /// Asynchronous address lookup.
        /// \note You can call this a second time after the last lookup has finished.
        Future<bool> lookup(std::string hostname, uint16_t port =0);

        /// Returns the primary address, either IPv4 or IPv6.
        struct ::sockaddr const* primaryAddress() const;

        /// Returns the primary address of whatever address family you pass.
        /// For convenience you can also pass 4 instead of AF_INET, or 6 instead of AF_INET6.
        struct ::sockaddr const* primaryAddress(int af) const;

        /// The primary address converted to a numeric string.
        std::string primaryAddressString() const;

    private:
        struct ::addrinfo* _info = nullptr;
    };


    /** A TCP socket. */
    class TCPSocket {
    public:
        TCPSocket();
        ~TCPSocket();

        /// Connects to an address/port. The address may be a hostname or dotted-quad IPv4 address.
        Future<void> connect(std::string const& address, uint16_t port);

        /// Returns true while the socket is connected.
        bool isOpen() const {return _socket != nullptr;}

        /// Returns a reference to a Generator that yields data received from the file.
        /// You can call this multiple times; it always returns the same Generator.
        Generator<std::string>& reader();

        /// Writes to the socket.
        Future<void> write(std::string);

        /// Closes the write stream, leaving the read stream open until the peer closes it.
        Future<void> shutdown();

        /// Closes the socket entirely. (Called by the destructor.)
        void close();

    private:
        Generator<std::string> _createReader();

        uv_tcp_s*                             _tcpHandle;
        uv_stream_s*                          _socket = nullptr;
        std::optional<Generator<std::string>> _reader;
    };

}
