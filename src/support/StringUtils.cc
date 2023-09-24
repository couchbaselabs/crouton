//
// StringUtils.cc
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

#include "StringUtils.hh"
#include "Bytes.hh"

namespace crouton {

    bool equalIgnoringCase(string_view a, string_view b) {
        size_t len = a.size();
        if (len != b.size())
            return false;
        for (size_t i = 0; i < len; i++) {
            if (toLower(a[i]) != toLower(b[i]))
                return false;
        }
        return true;
    }


    string hexString(ConstBytes bytes) {
        static const char kDigits[17] = "0123456789abcdef";
        std::string result;
        result.reserve(2 * bytes.size());
        for (byte b : bytes) {
            result += kDigits[uint8_t(b) >> 4];
            result += kDigits[uint8_t(b) & 0xF];
        }
        return result;
    }


    string decodeHexString(string_view hex) {
        size_t len = hex.size() & ~1;
        string result(len / 2, 0);
        uint8_t* dst = (uint8_t*)&result[0];
        for (size_t i = 0; i < len; i += 2)
            *dst++ = uint8_t((hexDigitToInt(hex[i]) << 4) | hexDigitToInt(hex[i+1]));
        return result;
    }


    std::pair<string_view,string_view>
    split(string_view str, char c) {
        if (auto p = str.find(c); p != string::npos)
            return {str.substr(0, p), str.substr(p + 1)};
        else
            return {str, ""};
    }


    void replaceStringInPlace(string &str,
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
