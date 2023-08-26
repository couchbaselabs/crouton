//
// AddrInfo.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "AddrInfo.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include <unistd.h>

namespace crouton {
    using namespace std;


#pragma mark - DNS LOOKUP:


    class getaddrinfo_request : public Request<uv_getaddrinfo_s> {
    public:
        static void callback(uv_getaddrinfo_s *req, int status, struct addrinfo *res) {
            auto self = static_cast<getaddrinfo_request*>(req);
            self->info = res;
            self->callbackWithStatus(req, status);
        }

        struct addrinfo* info = nullptr;
    };


    AddrInfo& AddrInfo::operator=(AddrInfo&& ai) {
        uv_freeaddrinfo(_info);
        _info = ai._info;
        ai._info = nullptr;
        return *this;
    }


    AddrInfo::~AddrInfo() {
        uv_freeaddrinfo(_info);
    }


    Future<AddrInfo> AddrInfo::lookup(string hostName, uint16_t port) {
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
        check(uv_getaddrinfo(curLoop(), &req, req.callback,
                                 hostName.c_str(), service, &hints),
                  "looking up hostname");
        check( AWAIT req, "looking up hostname" );

        RETURN AddrInfo(req.info);
    }


    sockaddr const* AddrInfo::_primaryAddress(int ipv) const {
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

    sockaddr const& AddrInfo::primaryAddress(int af) const {
        if (auto addr = _primaryAddress(af))
            return *addr;
        else
            throw UVError("getting address of hostname", UV__EAI_ADDRFAMILY);
    }

    sockaddr const& AddrInfo::primaryAddress() const {
        auto addr = _primaryAddress(4);
        if (!addr)
            addr = _primaryAddress(6);
        if (addr)
            return *addr;
        else
            throw UVError("getting address of hostname", UV__EAI_ADDRFAMILY);
    }

    std::string AddrInfo::primaryAddressString() const {
        char buf[100];
        auto &addr = primaryAddress();
        int err;
        if (addr.sa_family == PF_INET)
            err = uv_ip4_name((struct sockaddr_in*)&addr, buf, sizeof(buf) - 1);
        else
            err = uv_ip6_name((struct sockaddr_in6*)&addr, buf, sizeof(buf) - 1);
        return err ? "" : buf;
    }

}
