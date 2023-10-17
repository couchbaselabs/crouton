//
// AddrInfo.hh
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
#include "Future.hh"

#ifdef ESP_PLATFORM
    struct ip_addr;
#else
    struct addrinfo;
    struct sockaddr;
#endif

namespace crouton::io {

    /** An asynchronous DNS lookup. */
    class AddrInfo {
    public:
#ifdef ESP_PLATFORM
        using RawAddress = ::ip_addr;
#else
        using RawAddress = ::sockaddr;
#endif

        /// Does a DNS lookup of the given hostname, returning an AddrInfo or an error.
        staticASYNC<AddrInfo> lookup(string hostname, uint16_t port =0);

        /// Returns the primary address, which may be either IPv4 or IPv6.
        RawAddress const& primaryAddress() const;

        /// Returns (a pointer to) the primary address of whichever address family you pass,
        /// or nullptr if none.
        /// @note For convenience you can also pass 4 instead of AF_INET, or 6 instead of AF_INET6.
        RawAddress const* primaryAddress(int af) const;

        /// The primary address converted to a numeric string.
        string primaryAddressString() const;

    private:
#ifdef ESP_PLATFORM
        using addrinfo = ::ip_addr;
        using deleter = std::default_delete<addrinfo>;
#else
        using addrinfo = ::addrinfo;
        struct deleter { void operator() (addrinfo*); };
#endif

        explicit AddrInfo(addrinfo*);

        std::unique_ptr<addrinfo,deleter> _info;
    };

}
