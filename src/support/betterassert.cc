//
// betterassert.cc
//
// Copyright © 2018 Couchbase. All rights reserved.
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

#include "betterassert.hh"
#include <exception> // for std::terminate()
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#include "asprintf.h"
#endif

#ifndef __cold
#define __cold
#endif

namespace crouton {

    __cold
    static const char* filename(const char *file) {
        const char *slash = strrchr(file, '/');
        if (!slash)
            slash = strrchr(file, '\\');
        if (slash)
            file = slash + 1;
        return file;
    }


    __cold
    static void default_assert_failed_hook(const char *msg) {
        fprintf(stderr, "\n***%s\n", msg);
    }

    void (*assert_failed_hook)(const char *message) = &default_assert_failed_hook;


    __cold
    static const char* log(const char *format, const char *cond, std::source_location const& loc)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        char *msg;
        if (asprintf(&msg, format, cond, loc.function_name(), filename(loc.file_name()), loc.line()) > 0) {
            assert_failed_hook(msg);
            // (Yes, this leaks 'msg'. Under the circumstances, not an issue.)
            return msg;
        } else {
            // Best we can do if even malloc has failed us:
            assert_failed_hook(format);
            return format;
        }
#pragma GCC diagnostic pop
    }


    __cold
    void _assert_failed(const char *cond, std::source_location const& loc) {
        log("FATAL: FAILED ASSERTION `%s` in %s (at %s line %d)",
            cond, loc);
        std::terminate();
    }

    __cold
    void _precondition_failed(const char *cond, std::source_location const& loc) {
        log("FATAL: FAILED PRECONDITION: `%s` not true when calling %s (at %s line %d)",
            cond, loc);
        std::terminate();
    }

    __cold
    void _postcondition_failed(const char *cond, std::source_location const& loc) {
        log("FATAL: FAILED POSTCONDITION: `%s` not true at end of %s (at %s line %d)",
            cond, loc);
        std::terminate();
    }

}
