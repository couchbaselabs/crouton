//
// StringUtils.hh
//
// 
//

#pragma once
#include "Base.hh"
#include <cassert>

namespace crouton {

    /// Plain-ASCII version of `tolower`, with no nonsense about locales or ints.
    inline char toLower(char c) {
        if (c >= 'A' && c <= 'Z')
            c += 32;
        return c;
    }

    /// Plain-ASCII version of `toupper`, with no nonsense about locales or ints.
    inline char toUpper(char c) {
        if (c >= 'a' && c <= 'z')
            c -= 32;
        return c;
    }

    inline bool isAlphanumeric(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    inline bool isHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    inline bool isURLSafe(char c) {
        return isAlphanumeric(c) || strchr("-_.~", c) != nullptr;
    }

    inline int hexDigitToInt(char c) {
        if (c < 'A') 
            return c - '0';
        if (c < 'a') 
            return c - 'A' + 10;
        return c - 'a' + 10;
    }

    inline char asHexDigit(int n) {
        assert(n >= 0 && n < 16);
        if (n < 10) 
            return '0' + char(n);
        return 'A' + char(n - 10);
    }

    /// Lowercases a string.
    static inline string toLower(string str) {
        for (char &c : str)
            c = toLower(c);
        return str;
    }

    static inline bool equalIgnoringCase(string_view a, string_view b) {
        size_t len = a.size();
        if (len != b.size())
            return false;
        for (size_t i = 0; i < len; i++) {
            if (toLower(a[i]) != toLower(b[i]))
                return false;
        }
        return true;
    }

    inline std::pair<string_view,string_view>
    split(string_view str, char c) {
        if (auto p = str.find(c); p != string::npos)
            return {str.substr(0, p), str.substr(p + 1)};
        else
            return {str, ""};
    }

    inline void replaceStringInPlace(string &str,
                                            string_view substring,
                                            string_view replacement)
    {
        string::size_type pos = 0;
        while((pos = str.find(substring, pos)) != string::npos) {
            str.replace(pos, substring.size(), replacement);
            pos += replacement.size();
        }
    }

}
