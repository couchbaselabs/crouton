//
// MiniFormat.cc
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

#include "util/MiniFormat.hh"
#include <iostream>
#include <sstream>

namespace crouton::minifmt {
    using namespace std;
    using namespace i;

    void vformat_types(ostream& out, string_view fmt, FmtIDList types, va_list args) {
        auto itype = types;
        size_t pos;
        while (string::npos != (pos = fmt.find_first_of("{}"))) {
            if (pos > 0)
                out << fmt.substr(0, pos);

            if (fmt[pos] == '}') [[unlikely]] {
                // (The only reason to pay attention to "}" is that the std::format spec says
                // "}}" is an escape and should be emitted as "}". Otherwise a "} is a syntax
                // error, but let's just emit it as-is.
                out << '}';
                if (pos < fmt.size() - 1 && fmt[pos + 1] == '}')
                    ++pos;
                fmt = fmt.substr(pos + 1);
            } else if (pos < fmt.size() - 1 && fmt[pos + 1] == '{') {
                // "{{" is an escape; emit "{".
                out << '{';
                fmt = fmt.substr(pos + 2);
            } else {
                pos = fmt.find('}', pos + 1);
                //TODO: Pay attention to at least some formatting specs
                fmt = fmt.substr(pos + 1);
                switch( *(itype++) ) {
                    case FmtID::None:       out << "{{{TOO FEW ARGS}}}"; return;
                    case FmtID::Bool:       out << (va_arg(args, int) ? "true" : "false"); break;
                    case FmtID::Char:       out << char(va_arg(args, int)); break;
                    case FmtID::Int:        out << va_arg(args, int); break;
                    case FmtID::UInt:       out << va_arg(args, unsigned int); break;
                    case FmtID::Long:       out << va_arg(args, long); break;
                    case FmtID::ULong:      out << va_arg(args, unsigned long); break;
                    case FmtID::LongLong:   out << va_arg(args, long long); break;
                    case FmtID::ULongLong:  out << va_arg(args, unsigned long long); break;
                    case FmtID::Double:     out << va_arg(args, double); break;
                    case FmtID::CString:    out << va_arg(args, const char*); break;
                    case FmtID::Pointer:    out << va_arg(args, const void*); break;
                    case FmtID::String:     out << *va_arg(args, const string*); break;
                    case FmtID::StringView: out << *va_arg(args, const string_view*); break;
                    case FmtID::Write:      out << *va_arg(args, const write*); break;
                }
            }
        }

        if (!fmt.empty())
            out << fmt;
        if (*itype != FmtID::None)
            out << "{{{TOO FEW PLACEHOLDERS}}}";
    }

    void format_types(ostream& out, string_view fmt, FmtIDList types, ...) {
        va_list args;
        va_start(args, types);
        vformat_types(out, fmt, types, args);
        va_end(args);
    }

    string vformat_types(string_view fmt, FmtIDList types, va_list args) {
        stringstream out;
        vformat_types(out, fmt, types, args);
        return out.str();
    }

    string format_types(string_view fmt, FmtIDList types, ...) {
        va_list args;
        va_start(args, types);
        string result = vformat_types(fmt, types, args);
        va_end(args);
        return result;
    }

}
