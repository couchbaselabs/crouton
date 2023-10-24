//
// tests.hh
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

#include "Crouton.hh"
#include "util/Logging.hh"
#include <iostream>

#include "catch_amalgamated.hpp"

using namespace std;
using namespace crouton;


// Runs a coroutine that returns `Future<void>`, returning once it's completed.
void RunCoroutine(Future<void> (*test)());


template <typename T>
T waitFor(Future<T>&& f) {
    bool ready = false;
    (void) f.template then([&](T const&) {ready = true;});
    Scheduler::current().runUntil([&]{ return ready; });
    return std::move(f).result();
}

template <>
inline void waitFor(Future<void>&& f) {
    bool ready = false;
    (void) f.then([&]() {ready = true;});
    Scheduler::current().runUntil([&]{ return ready; });
}


Future<string> readFile(string const& path);
