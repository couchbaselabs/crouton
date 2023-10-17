//
// Backtrace+ESP32.cc
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

#include "Backtrace.hh"
#include "StringUtils.hh"
#include <esp_debug_helpers.h>
#include <iostream>

namespace fleece {
    using namespace std;

    namespace internal {

        int backtrace(void** buffer, size_t max) {
            esp_backtrace_frame_t frame;
            frame.exc_frame = nullptr;
            esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
            int n = 0;
            do {
                *buffer++ = (void*)frame.pc;
                ++n;
            } while (esp_backtrace_get_next_frame(&frame));
            return n;
        }

        char* unmangle(const char *function) {
            return (char*)function;
        }

    }


    bool Backtrace::writeTo(ostream &out) const {
        esp_backtrace_print(10);    //TODO
        return true;
    }

}
