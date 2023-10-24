//
// StringUtils.hh
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

#include <cstring>

namespace crouton {
    class ConstBytes;


    /// Plain-ASCII version of `tolower`, with no nonsense about locales or ints.
    Pure inline char toLower(char c) noexcept {
        if (c >= 'A' && c <= 'Z')
            c += 32;
        return c;
    }

    /// Plain-ASCII version of `toupper`, with no nonsense about locales or ints.
    Pure inline char toUpper(char c) noexcept {
        if (c >= 'a' && c <= 'z')
            c -= 32;
        return c;
    }

    Pure inline bool isAlphanumeric(char c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    Pure inline bool isHexDigit(char c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    /// Converts an ASCII hex digit to its numeric value.
    Pure inline int hexDigitToInt(char c) noexcept {
        if (c < 'A')
            return c - '0';
        if (c < 'a') 
            return c - 'A' + 10;
        return c - 'a' + 10;
    }

    /// Returns a number 0..15 converted to an ASCII hex digit.
    Pure inline char asHexDigit(int n) noexcept {
        assert(n >= 0 && n < 16);
        if (n < 10) 
            return '0' + char(n);
        return 'A' + char(n - 10);
    }

    /// True if a character can safely be used in a URL without escaping.
    Pure inline bool isURLSafe(char c) noexcept {
        return isAlphanumeric(c) || strchr("-_.~", c) != nullptr;
    }

    /// Lowercases a string.
    inline string toLower(string str) {
        for (char &c : str)
            c = toLower(c);
        return str;
    }

    /// Returns a string of hex
    string hexString(ConstBytes bytes);

    /// Converts a hex string to binary.
    string decodeHexString(string_view);

    /// Case-insensitive equality comparison (ASCII only!)
    Pure bool equalIgnoringCase(string_view a, string_view b) noexcept;

    /// Splits a string around the first occurrence of `c`;
    /// if there is none, assumes it's at the end, i.e. returns `{str, ""}`.
    Pure std::pair<string_view,string_view> split(string_view str, char c) noexcept;

    /// Splits a string at an index.
    /// @param str  The string to split
    /// @param pos  The index at which to split
    /// @param delimSize  The length of the delimiter being split around.
    Pure std::pair<string_view,string_view> 
        splitAt(string_view str, size_t pos, size_t delimSize = 0) noexcept;

    /// Replaces all occurrences of `substring` with `replacement`, in-place.
    void replaceStringInPlace(string &str,
                              string_view substring,
                              string_view replacement);
}
