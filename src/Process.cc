//
// Process.cc
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

#include "Process.hh"
#include "Logging.hh"
#include "Task.hh"
#include "UVBase.hh"
#include "UVInternal.hh"

namespace crouton {
    using namespace std;


    Args sArgs;


    static void initArgs(int argc, const char * argv[]) {
        auto nuArgv = uv_setup_args(argc, (char**)argv);
        sArgs.resize(argc);
        for (int i = 0; i < argc; ++i)
            sArgs[i] = nuArgv[i];
    }

    Args const& MainArgs() {
        return sArgs;
    }

    optional<string_view> Args::first() const {
        optional<string_view> arg;
        if (size() >= 1)
            arg = at(1);
        return arg;
    }

    optional<string_view> Args::popFirst() {
        optional<string_view> arg;
        if (size() >= 1) {
            arg = std::move(at(1));
            erase(begin() + 1);
        }
        return arg;
    }

    optional<string_view> Args::popFlag() {
        if (auto flag = first(); flag && flag->starts_with("-")) {
            popFirst();
            return flag;
        } else {
            return nullopt;
        }
    }



    int Main(int argc, const char * argv[], Future<int>(*fn)()) {
        try {
            initArgs(argc, argv);
            InitLogging();
            Future<int> fut = fn();
            Scheduler::current().runUntil([&]{ return fut.hasValue(); });
            return fut.value();
        } catch (std::exception const& x) {
            spdlog::error("*** Unexpected exception: {}" , x.what());
            return 1;
        } catch (...) {
            spdlog::error("*** Unexpected exception");
            return 1;
        }
    }

    int Main(int argc, const char * argv[], Task(*fn)()) {
        try {
            initArgs(argc, argv);
            Task task = fn();
            Scheduler::current().run();
            return 0;
        } catch (std::exception const& x) {
            spdlog::error("*** Unexpected exception: {}", x.what());
            return 1;
        } catch (...) {
            spdlog::error("*** Unexpected exception");
            return 1;
        }
    }

}
