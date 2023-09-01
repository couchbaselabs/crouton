//
// URL.cc
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

#include "URL.hh"
#include "StringUtils.hh"
#include <cstring>
#include <stdexcept>

namespace crouton {
    using namespace std;

    // Struct copied from tlsuv project, include/tlsuv/http.h
    struct tlsuv_url_s {
        const char *scheme;
        size_t scheme_len;
        const char *hostname;
        size_t hostname_len;
        uint16_t port;
        const char *path;
        size_t path_len;
        const char *query;
        size_t query_len;
    };

    static int tlsuv_parse_url(struct tlsuv_url_s *url, const char *urlstr);


    bool URLRef::tryParse(const char* str) {
        tlsuv_url_s url;
        if (tlsuv_parse_url(&url, str) != 0)
            return false;
        port = url.port;
        scheme = string_view(url.scheme, url.scheme_len);
        hostname = string_view(url.hostname, url.hostname_len);
        path = string_view(url.path, url.path_len);
        query = string_view(url.query, url.query_len);
        return true;
    }

    void URLRef::parse(const char* str) {
        if (!tryParse(str))
            throw invalid_argument("Invalid URL");
    }


    std::string URLRef::normalizedScheme() const {
        return toLower(string(scheme));
    }



    //-------- Function below copied from tlsuv project, src/http.c
    //         https://github.com/openziti/tlsuv.git

    // Copyright (c) NetFoundry Inc.
    //
    // Licensed under the Apache License, Version 2.0 (the "License");
    // you may not use this file except in compliance with the License.
    // You may obtain a copy of the License at
    //
    //     https://www.apache.org/licenses/LICENSE-2.0
    //
    // Unless required by applicable law or agreed to in writing, software
    // distributed under the License is distributed on an "AS IS" BASIS,
    // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    // See the License for the specific language governing permissions and
    // limitations under the License.

    /**
     * Zero-copy URL parser. [url] fields point to the parsed [urlstr].
     * @param url parsed URL structure
     * @param urlstr URL in string form
     * @return 0 on success, -1 on failure
     */
    static int tlsuv_parse_url(struct tlsuv_url_s *url, const char *urlstr) {
        memset(url, 0, sizeof(struct tlsuv_url_s));

        const char *p = urlstr;
        int count = 0;
        int rc = sscanf(p, "%*[^:]%n://", &count);
        if (rc == 0 &&
            (p + count)[0] == ':' && (p + count)[1] == '/' && (p + count)[2] == '/'
            ) {
            url->scheme = p;
            url->scheme_len = count;
            p += (count + 3);
        }

        count = 0;
        if (sscanf(p, "%*[^:/]%n", &count) == 0 && count > 0) {
            url->hostname = p;
            url->hostname_len = count;
            p += count;
        }

        if (*p == ':') {
            if (url->hostname == NULL)
                return -1;
            p += 1;
            char *pend;
            long lport = strtol(p, &pend, 10);

            if (pend == p)
                return -1;

            if (lport > 0 && lport <= UINT16_MAX) {
                url->port = (uint16_t)lport;
                p = pend;
            } else {
                return -1;
            }
        }

        if (*p == '\0')
            return 0;

        if (*p != '/') {
            return -1;
        }

        if (sscanf(p, "%*[^?]%n", &count) == 0) {
            url->path = p;
            url->path_len = count;
            p += count;
        }

        if (*p == '?') {
            url->query = p + 1;
            url->query_len = strlen(url->query);
        }

        return 0;
    }

}
