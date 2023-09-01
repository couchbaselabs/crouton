//
// StringUtils.hh
//
// 
//

#pragma once
#include <string>

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
    static inline std::string toLower(std::string str) {
        for (char &c : str)
            c = toLower(c);
        return str;
    }

}
