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
#include "util/Base.hh"


namespace crouton::io {

    /** A parsed version of a URL. The string properties point into the given string,
        and become invalid if that string changes or goes out of scope.
        If that's a problem, use `URL` instead.

        @note The string properties are all substrings of the input; nothing is unescaped. */
    class URLRef {
    public:
        URLRef() = default;
        explicit URLRef(const char* str)      {parse(str);}
        explicit URLRef(string const& str)    :URLRef(str.c_str()) { }

        URLRef(string_view scheme,
               string_view hostname,
               uint16_t port = 0,
               string_view path = "/",
               string_view query = "") noexcept;

        /// Parses a URL, updating the properties. Returns false on error.
        [[nodiscard]] bool tryParse(const char*) noexcept;

        /// Parses a URL, updating the properties. Throws CroutonError::InvalidURL on error.
        void parse(const char*);

        string_view scheme;
        string_view hostname;
        uint16_t    port = 0;
        string_view path;
        string_view query;

        /// Lowercased version of `scheme`
        string normalizedScheme() const;

        /// Returns the path with URL escapes decoded.
        string unescapedPath() const   {return unescape(path);}

        /// Returns the value for a key in the query, or "" if not found.
        string_view queryValueForKey(string_view key) noexcept pure;

        /// Recombines the parts back into a URL. Useful if you've changed them.
        string reencoded() const;

        //---- static utility functions:

        /// URL-escapes ("percent-escapes") a string.
        /// If `except `is given, characters in that string will not be escaped.
        static string escape(string_view, const char* except = nullptr);

        /// Decodes a URL-escaped string.
        static string unescape(string_view);
    };


    /** A parsed version of a URL. Contains a copy of the string. */
    class URL : public URLRef {
    public:
        explicit URL(string&& str)     :URLRef(), _str(std::move(str)) {parse(_str.c_str());}
        explicit URL(string_view str)  :URL(string(str)) { }
        explicit URL(const char* str)  :URL(string(str)) { }

        URL(string_view scheme,
            string_view hostname,
            uint16_t port = 0,
            string_view path = "/",
            string_view query = "");

        URL(URL const& url)                 :URL(url._str) { }
        URL& operator=(URL const& url)      {_str = url._str; parse(_str.c_str()); return *this;}
        URL(URL&& url) noexcept             :URL(std::move(url._str)) { }
        URL& operator=(URL&& url)           {_str = std::move(url._str); parse(_str.c_str()); return *this;}

        string const& asString() const noexcept pure {return _str;}
        operator string() const             {return _str;}

        void reencode() {
            _str = reencoded();
            parse(_str.c_str());
        }

    private:
        string _str;
    };

}
