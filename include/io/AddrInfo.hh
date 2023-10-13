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

struct addrinfo;
struct sockaddr;

namespace crouton::io {

    /** An asynchronous DNS lookup. */
    class AddrInfo {
    public:

        /// Does a DNS lookup of the given hostname, returning an AddrInfo or an error.
        staticASYNC<AddrInfo> lookup(string hostname, uint16_t port =0);

        /// Returns the primary address, which may be either IPv4 or IPv6.
        sockaddr const& primaryAddress() const;

        /// Returns the primary address of whichever address family you pass.
        /// If there is none, throws `UV__EAI_ADDRFAMILY`.
        /// @note For convenience you can also pass 4 instead of AF_INET, or 6 instead of AF_INET6.
        sockaddr const& primaryAddress(int af) const;

        /// The primary address converted to a numeric string.
        string primaryAddressString() const;

        ~AddrInfo();
        AddrInfo(AddrInfo&& ai) noexcept :_info(ai._info) {ai._info = nullptr;}
        AddrInfo& operator=(AddrInfo&& ai) noexcept;

    private:
        explicit AddrInfo(struct ::addrinfo* info) noexcept :_info(info) { }
        AddrInfo(AddrInfo const&) = delete;
        AddrInfo& operator=(AddrInfo const&) = delete;
        sockaddr const* _primaryAddress(int af) const;

        struct ::addrinfo* _info;
    };

}
