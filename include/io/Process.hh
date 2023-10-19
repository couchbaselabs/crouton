//
// Process.hh
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
#include "Future.hh"

#include <optional>
#include <vector>

namespace crouton::io {

    /** Main process function that runs an event loop and calls `fn`, which returns a Future.
        When/if the Future resolves, it returns its value as the process status,
        or if the value is an exception it logs an error and returns 1. */
    int Main(int argc, const char* argv[], Future<int> (*fn)());

    /** Main process function that runs an event loop and calls `fn`, which returns a Task.
        The event loop forever or until stopped. */
    int Main(int argc, const char* argv[], Task (*fn)());


#ifndef ESP_PLATFORM
    /** Convenience for defining the program's `main` function.
        `FUNC` should be the name of a coroutine function returning `Task` or `Future<void>`. */
    #define CROUTON_MAIN(FUNC) \
        int main(int argc, const char* argv[]) {return crouton::io::Main(argc, argv, FUNC);}
#else
    /** Convenience for defining the firmware's `app_main` function.
        `FUNC` should be the name of a coroutine function returning `Task` or `Future<void>`. */
    #define CROUTON_MAIN(FUNC) \
        extern "C" void app_main() {crouton::io::Main(0, nullptr, FUNC);}
#endif


    /** Simple wrapper around command-line arguments. */
    class Args : public std::vector<string_view> {
    public:
        /** The first argument, if any. */
        std::optional<string_view> first() const;

        /** Removes and returns the first arg, if any. */
        std::optional<string_view> popFirst();

        /** Removes and returns the first arg, but only if it starts with "-". */
        std::optional<string_view> popFlag();
    };


    /// The command-line arguments, as captured by `Main`.
    /// Make a copy if you want to use methods like `popFirst`.
    extern const Args& MainArgs();


    /** Information about an output device; currently just color support. */
    struct TTY {
        static const TTY out;   ///< TTY instance for stdout
        static const TTY err;   ///< TTY instance for stderr

        explicit TTY(int fd);

        const bool color;       ///< True if device supports ANSI color escapes

        // ANSI escape sequences, or empty if no color support:
        const char* const bold;
        const char* const dim;
        const char* const italic;
        const char* const underline;
        const char* const red;
        const char* const yellow;
        const char* const green;
        const char* const reset;
    };

}
