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

namespace crouton {
    using namespace std;


    string const& Memoized::lookup(const void* addr) {
        unique_lock<mutex> lock(_mutex);
        auto i = _known.find(addr);
        if (i == _known.end())
            i = _known.insert({addr, _compute(addr)}).first;
        return i->second;
    }


    string const& GetTypeName(type_info const& info) {
        static Memoized sTypeNames([](const void* addr) -> string {
            string name = fleece::Unmangle(*(type_info*)addr);      // Get unmangled name
            if (auto p = name.find('<'); p != string::npos)         // Strip template params
                name = name.substr(0, p);
            if (auto p = name.find_last_of(":"); p != string::npos) // Strip namespaces
                name = name.substr(p + 1);
            if (name.ends_with("Impl"))                             // Strip -Impl suffix
                name = name.substr(0, name.size() - 4);
            return name;
        });
        return sTypeNames.lookup(&info);
    }


    string const& GetFunctionName(const void* addr) {
        static Memoized sFnNames([](const void* addr) -> string {
            std::string symbol = fleece::FunctionName(addr);
            if (symbol.ends_with(" (.resume)"))
                symbol = symbol.substr(0, symbol.size() - 10);
            else if (symbol.ends_with(" (.destroy)"))
                symbol = symbol.substr(0, symbol.size() - 11);
            if (symbol.starts_with("crouton::"))
                symbol = symbol.substr(9);
            if (auto pos = symbol.find('('); pos != string::npos)
                symbol = symbol.substr(0, pos);
            return symbol;
        });
        return sFnNames.lookup(addr);
    }

}
