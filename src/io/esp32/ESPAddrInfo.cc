//
// ESPAddrInfo.cc
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

#include "io/AddrInfo.hh"
#include "ESPBase.hh"
#include "CoCondition.hh"
#include "Logging.hh"
#include <lwip/dns.h>

namespace crouton::io {
    using namespace std;
    using namespace crouton::io::esp;


    Future<AddrInfo> AddrInfo::lookup(string hostname, uint16_t port) {
        Blocker<ip_addr_t> blocker;
        auto callback = [] (const char *name, const ip_addr_t *ipaddr, void *ctx) {
            // Warning: Callback is called on the lwip thread
            auto b = (Blocker<ip_addr_t>*)ctx;
            if (ipaddr) {
                b->notify(*ipaddr);
            } else {
                b->notify(ip_addr_t{.u_addr = {}, .type = 0xFF});
            }
        };

        ip_addr addr;
        switch (err_t err = dns_gethostbyname(hostname.c_str(), &addr, callback, &blocker)) {
            case ERR_OK:
                RETURN AddrInfo(&addr);
            case ERR_INPROGRESS:
                LNet->debug("Awaiting DNS lookup of {}", hostname);
                addr = AWAIT blocker;
                LNet->debug("DNS lookup {}", (addr.type != 0xFF ? "succeeded" : "failed"));
                if (addr.type != 0xFF)
                    RETURN AddrInfo(&addr);
                else
                    RETURN ESPError::HostNotFound;
            default:
                RETURN Error(LWIPError(err));
        }
    }


    AddrInfo::AddrInfo(ip_addr* addr)
    :_info(make_unique<ip_addr>(*addr))
    { }

    ip_addr const& AddrInfo::primaryAddress() const {
        return *_info;
    }

    ip_addr const* AddrInfo::primaryAddress(int af) const {
        if (af == 4)
            return _info.get();
        else
            Error(ESPError::HostNotFound).raise();
    }

    /// The primary address converted to a numeric string.
    string AddrInfo::primaryAddressString() const {
        char buf[32];
        return string(ipaddr_ntoa_r(_info.get(), buf, sizeof(buf)));
    }


}
