//
// URL.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "URL.hh"
#include <stdexcept>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#endif

#include "http.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif


namespace crouton {
    using namespace std;

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


}
