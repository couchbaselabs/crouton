//
// AddrInfo.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "UVBase.hh"
#include <string>

struct addrinfo;
struct sockaddr;

namespace snej::coro::uv {

    /** An asynchronous DNS lookup. */
    class AddrInfo {
    public:
        AddrInfo() = default;
        ~AddrInfo();

        /// Asynchronous address lookup.
        /// \note You can call this a second time after the last lookup has finished.
        [[nodiscard]] Future<bool> lookup(std::string hostname, uint16_t port =0);

        /// Returns the primary address, either IPv4 or IPv6.
        struct ::sockaddr const* primaryAddress() const;

        /// Returns the primary address of whichever address family you pass.
        /// For convenience you can also pass 4 instead of AF_INET, or 6 instead of AF_INET6.
        struct ::sockaddr const* primaryAddress(int af) const;

        /// The primary address converted to a numeric string.
        std::string primaryAddressString() const;

    private:
        struct ::addrinfo* _info = nullptr;
        bool _busy = false;
    };

}
