//
// StringUtils.hh
//
// 
//

#pragma once
#include <string_view>   

namespace crouton {

    /// Plain-ASCII version of `tolower`, with no nonsense about locales or ints.
    static inline char toLower(char c) {
        if (c >= 'A' && c <= 'Z')
            c += 32;
        return c;
    }

    /// Plain-ASCII version of `toupper`, with no nonsense about locales or ints.
    static inline char toUpper(char c) {
        if (c >= 'a' && c <= 'z')
            c -= 32;
        return c;
    }

    static inline bool isAlphanumeric(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
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

}
