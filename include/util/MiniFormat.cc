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

#include "MiniFormat.hh"
#include <iostream>
#include <sstream>

namespace crouton::minifmt {
    using namespace std;
    using namespace i;

    void _vformat(ostream& out, string_view fmt, std::initializer_list<FmtID> types, va_list args) {
        auto itype = types.begin();
        size_t pos;
        while (string::npos != (pos = fmt.find('{'))) {
            if (pos > 0)
                out << fmt.substr(0, pos);
            pos = fmt.find('}', pos + 1);
            fmt = fmt.substr(pos + 1);
            if (itype == types.end()) {
                out << "{{{TOO FEW ARGS}}}";
                return;
            }
            switch( *(itype++) ) {
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
                case FmtID::None:       abort();
            }
        }
        if (!fmt.empty())
            out << fmt;
        if(itype != types.end())
            out << "{{{TOO FEW PLACEHOLDERS}}}";
    }

    void _format(ostream& out, string_view fmt, std::initializer_list<FmtID> types, ...) {
        va_list args;
        va_start(args, types);
        _vformat(out, fmt, types, args);
        va_end(args);
    }

    string _vformat(string_view fmt, std::initializer_list<FmtID> types, va_list args) {
        stringstream out;
        _vformat(out, fmt, types, args);
        return out.str();
    }

    string _format(string_view fmt, std::initializer_list<FmtID> types, ...) {
        va_list args;
        va_start(args, types);
        string result = _vformat(fmt, types, args);
        va_end(args);
        return result;
    }

}
