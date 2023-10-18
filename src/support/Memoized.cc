//
// Memoized.cc
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

#include "Memoized.hh"
#include "Backtrace.hh"
#include "StringUtils.hh"

namespace crouton {
    using namespace std;


    string const& Memoized::lookup(const void* addr) {
        unique_lock<mutex> lock(_mutex);
        auto i = _known.find(addr);
        if (i == _known.end())
            i = _known.insert({addr, _compute(addr)}).first;
        return i->second;
    }


    static void cleanup(string &name) {
        if (name.starts_with("crouton::"))
            name = name.substr(9);
        replaceStringInPlace(name, "std::__1::", "std::");  // probably libc++ specific
        replaceStringInPlace(name, "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string");
    }


    string const& GetTypeName(type_info const& info) {
#if CROUTON_RTTI
        static Memoized sTypeNames([](const void* addr) -> string {
            string name = fleece::Unmangle(*(type_info*)addr);      // Get unmangled name
            cleanup(name);
            return name;
        });
        return sTypeNames.lookup(&info);
#else
        static const string name = "???";
        return name;
#endif
    }


    string const& GetFunctionName(const void* addr) {
        static Memoized sFnNames([](const void* addr) -> string {
            string name = fleece::FunctionName(addr);
            if (name.ends_with(" (.resume)"))
                name = name.substr(0, name.size() - 10);
            else if (name.ends_with(" (.destroy)"))
                name = name.substr(0, name.size() - 11);
            if (name.starts_with("crouton::"))
                name = name.substr(9);
            if (auto pos = name.find('('); pos != string::npos)
                name = name.substr(0, pos);
            cleanup(name);
            return name;
        });
        return sFnNames.lookup(addr);
    }

}
