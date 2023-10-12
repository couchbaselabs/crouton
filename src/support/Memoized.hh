//
// Memoized.hh
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
#include <mutex>
#include <typeinfo>
#include <unordered_map>

namespace crouton {

    /** Maps a void* to string, computing the string only once for each unique address. */
    class Memoized {
    public:
        using ComputeFn = string(*)(const void*);

        explicit Memoized(ComputeFn fn) :_compute(fn) { }

        string const& lookup(const void* addr);

    private:
        ComputeFn _compute;
        std::mutex _mutex;
        std::unordered_map<const void*, string> _known;
    };


    /** Returns the unmangled, cleaned-up form of a C++ type name. */
    string const& GetTypeName(std::type_info const&);

    /** Returns the unmangled, cleaned-up form of a function's name,
        given its address from a backtrace.*/
    string const& GetFunctionName(const void* address);

}
