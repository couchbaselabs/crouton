//
// AddrInfo.cc
//
// 
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

#include "AddrInfo.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include <unistd.h>

namespace snej::coro::uv {
    using namespace std;


#pragma mark - DNS LOOKUP:


    class getaddrinfo_request : public uv::RequestWithStatus<uv_getaddrinfo_s> {
    public:
        static void callback(uv_getaddrinfo_s *req, int status, struct addrinfo *res) {
            auto self = static_cast<getaddrinfo_request*>(req);
            self->info = res;
            self->callbackWithStatus(req, status);
        }

        struct addrinfo* info = nullptr;
    };


    AddrInfo::~AddrInfo() {
        uv_freeaddrinfo(_info);
    }


    Future<void> AddrInfo::lookup(string hostName, uint16_t port) {
        NotReentrant nr(_busy);
        uv_freeaddrinfo(_info);
        _info = nullptr;

        struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
        };

        const char* service = nullptr;
        char portStr[10];
        if (port != 0) {
            snprintf(portStr, 10, "%u", port);
            service = portStr;  // This causes the 'port' fields of the addrinfos to be filled in
        }

        getaddrinfo_request req;
        uv::check(uv_getaddrinfo(curLoop(), &req, req.callback,
                                 hostName.c_str(), service, &hints),
                  "looking up hostname");
        uv::check( AWAIT req, "looking up hostname" );

        _info = req.info;
    }


    struct ::sockaddr const* AddrInfo::primaryAddress(int ipv) const {
        assert(_info);
        int af = ipv;
        switch (ipv) {
            case 4: af = AF_INET; break;
            case 6: af = AF_INET6; break;
        }

        for (auto i = _info; i; i = i->ai_next) {
            if (i->ai_socktype == SOCK_STREAM && i->ai_protocol == IPPROTO_TCP && i->ai_family == af)
                return i->ai_addr;
        }
        return nullptr;
    }

    struct ::sockaddr const* AddrInfo::primaryAddress() const {
        auto addr = primaryAddress(4);
        return addr ? addr : primaryAddress(6);
    }

    std::string AddrInfo::primaryAddressString() const {
        char buf[100];
        auto *addr = primaryAddress();
        if (!addr)
            return "";
        int err;
        if (addr->sa_family == PF_INET)
            err = uv_ip4_name((struct sockaddr_in*)addr, buf, sizeof(buf) - 1);
        else
            err = uv_ip6_name((struct sockaddr_in6*)addr, buf, sizeof(buf) - 1);
        return err ? "" : buf;
    }

}
