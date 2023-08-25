//
// URL.cc
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


namespace snej::coro::uv {
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
