//
// URL.hh
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
#include "Base.hh"
#include <utility>

namespace crouton {

    /** A parsed version of a URL. The string properties point into the given string,
        and become invalid if that string changes or goes out of scope.
        If that's a problem, use `URL` instead.

        @note The string properties are all substrings of the input; nothing is unescaped. */
    class URLRef {
    public:
        URLRef() = default;
        explicit URLRef(const char* str)           {parse(str);}
        explicit URLRef(string const& str)    :URLRef(str.c_str()) { }

        /// Parses a URL, updating the properties. Returns false on error.
        [[nodiscard]] bool tryParse(const char*);

        /// Parses a URL, updating the properties. Throws std::invalid_argument on error.
        void parse(const char*);

        string_view scheme;
        string_view hostname;
        uint16_t    port = 0;
        string_view path;
        string_view query;

        /// Lowercased version of `scheme`
        string normalizedScheme() const;

    protected:
    };


    /** A parsed version of a URL. Contains a copy of the string. */
    class URL : public URLRef {
    public:
        explicit URL(string&& str)     :URLRef(), _str(std::move(str)) {parse(_str.c_str());}
        explicit URL(string_view str)  :URL(string(str)) { }
        explicit URL(const char* str)       :URL(string(str)) { }

        URL(URL const& url)                 :URL(url.asString()) { }
        URL& operator=(URL const& url)      {_str = url._str; parse(_str.c_str()); return *this;}

        string const& asString() const {return _str;}
        operator string() const        {return _str;}

    private:
        string _str;
    };

}
