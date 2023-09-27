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
#include "Error.hh"
#include "StringUtils.hh"
#include <cstring>

namespace crouton::io {
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
            Error::raise(CroutonError::InvalidURL);
    }


    string URLRef::normalizedScheme() const {
        return toLower(string(scheme));
    }


    string URLRef::unescape(string_view str) {
        size_t len = str.size();
        string result;
        result.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            char c = str[i];
            if (c == '%' && i + 2 < len && isHexDigit(str[i+1]) && isHexDigit(str[i+2])) {
                auto unescaped = uint8_t(16 * hexDigitToInt(str[i+1]) + hexDigitToInt(str[i+2]));
                c = char(unescaped);
                i += 2;
            }
            result.append(&c, 1);
        }
        return result;
    }


    string URLRef::reencoded() const {
        string result(scheme);
        if (!scheme.empty())
            result.append("://");
        result.append(hostname);
        if (port != 0) {
            result.append(":");
            result += to_string(port);
        }
        result += path;
        if (!query.empty()) {
            result.append("?");
            result += query;
        }
        return result;
    }


    URLRef::URLRef(string_view scheme_,
                   string_view hostname_,
                   uint16_t port_,
                   string_view path_,
                   string_view query_)
    :scheme(scheme_)
    ,hostname(hostname_)
    ,port(port_)
    ,path(path_)
    ,query(query_)
    { }

    
    URL::URL(string_view scheme_,
           string_view hostname_,
           uint16_t port_,
           string_view path_,
           string_view query_)
    :URLRef(scheme_, hostname_, port_, path_, query_)
    {
        reencode();
    }


    string URLRef::escape(string_view str, const char* except) {
        size_t len = str.size();
        string result;
        result.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            if (char c = str[i]; isURLSafe(c) || (except && strchr(except, c))) {
                result.append(&c, 1);
            } else {
                auto n = uint8_t(c);
                char buf[3] = {'%', asHexDigit(n >> 4), asHexDigit(n & 0x0F)};
                result.append(buf, 3);
            }
        }
        return result;
    }


    string_view URLRef::queryValueForKey(string_view key) {
        string_view remaining = query;
        while (!remaining.empty()) {
            string_view q;
            tie(q, remaining) = split(remaining, '&');
            auto [k, v] = split(q, '=');
            if (k == key)
                return v.empty() ? k : v;
        }
        return "";
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
