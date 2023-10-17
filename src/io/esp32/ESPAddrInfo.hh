//
// ESPAddrInfo.hh
//
// 
//

#pragma once
#include "Future.hh"

struct ip_addr;

namespace crouton::esp {

    /** An asynchronous DNS lookup. */
    class AddrInfo {
    public:

        /// Does a DNS lookup of the given hostname, returning an AddrInfo or an error.
        staticASYNC<AddrInfo> lookup(string hostname, uint16_t port =0);

        /// Returns the primary address, which may be either IPv4 or IPv6.
        ip_addr const& primaryAddress() const       {return *_addr;}

        /// The primary address converted to a numeric string.
        string primaryAddressString() const;

    private:
        explicit AddrInfo(ip_addr const&);

        std::unique_ptr<ip_addr> _addr;
    };


}
