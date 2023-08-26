//
// AddrInfo.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include "Future.hh"
#include "UVBase.hh"
#include <string>

struct addrinfo;
struct sockaddr;

namespace crouton {

    /** An asynchronous DNS lookup. */
    class AddrInfo {
    public:

        /// Does a DNS lookup of the given hostname, returning an AddrInfo or an exception.
        [[nodiscard]] static Future<AddrInfo> lookup(std::string hostname, uint16_t port =0);

        /// Returns the primary address, which may be either IPv4 or IPv6.
        sockaddr const& primaryAddress() const;

        /// Returns the primary address of whichever address family you pass.
        /// If there is none, throws `UV__EAI_ADDRFAMILY`.
        /// @note For convenience you can also pass 4 instead of AF_INET, or 6 instead of AF_INET6.
        sockaddr const& primaryAddress(int af) const;

        /// The primary address converted to a numeric string.
        std::string primaryAddressString() const;

        ~AddrInfo();
        AddrInfo(AddrInfo&& ai) :_info(ai._info) {ai._info = nullptr;}
        AddrInfo& operator=(AddrInfo&& ai);

    private:
        AddrInfo(struct ::addrinfo* info) :_info(info) { }
        AddrInfo(AddrInfo const&) = delete;
        AddrInfo& operator=(AddrInfo const&) = delete;
        sockaddr const* _primaryAddress(int af) const;

        struct ::addrinfo* _info;
    };

}
