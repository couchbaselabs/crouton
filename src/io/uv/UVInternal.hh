//
// UVInternal.hh
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
#include "CoCondition.hh"
#include "Coroutine.hh"
#include "Internal.hh"
#include "util/Bytes.hh"
#include "Logging.hh"
#include "Scheduler.hh"
#include "io/uv/UVBase.hh"

#include <algorithm>
#include <concepts>

#include <uv.h>
// On Windows <uv.h> drags in <windows.h>, which defines `min` and `max` as macros,
// which creates crazy syntax errors when calling std::min/max...
#undef min
#undef max



namespace crouton::io::uv {

    /// Checks a libuv function result and throws a UVError exception if it's negative.
    static inline void check(std::signed_integral auto status, const char* what) {
        if (status < 0)
            Error::raise(UVError(status), what);
    }

    #define CHECK_RETURN(STATUS, WHAT) \
        if (auto _status_ = (STATUS); _status_ >= 0) {} else co_return Error(UVError(_status_), (WHAT))


    /// Convenience function that returns `Scheduler::current().uvLoop()`.
    uv_loop_s* curLoop();


    /// Closes any type compatible with `uv_handle_t`, and
    /// calls `delete` on the struct pointer after the close completes.
    template <class T>
    void closeHandle(T* &handle) {
        if (handle) {
            handle->data = nullptr;
            uv_close((uv_handle_t*)handle, [](uv_handle_t* h) noexcept {
                delete (T*)h;
            });
            handle = nullptr;
        }
    }


    /** An Awaitable subclass of a libUV request type, such as uv_write_s. */
    template <class UV_REQUEST_T>
    class AwaitableRequest : public UV_REQUEST_T, public Blocker<int> {
    public:
        explicit AwaitableRequest(const char* what)  :_what(what) { }

        /// Pass this as the callback to a UV call on this request.
        static void callback(UV_REQUEST_T *req, int status) {
            static_cast<AwaitableRequest*>(req)->notify(status);
        }

        int await_resume() noexcept {
            int result = Blocker<int>::await_resume();
            check(result, _what);
            return result;
        }

        const char* _what;
    };


}
