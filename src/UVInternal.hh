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
#include "Coroutine.hh"
#include "UVBase.hh"
#include "Scheduler.hh"
#include <uv.h>
#include <concepts>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace crouton {

    /// Checks a libuv function result and throws a UVError exception if it's negative.
    static inline void check(std::signed_integral auto status, const char* what) {
        if (status < 0) {
            UVError x(what, int(status));
            std::cerr << "** libuv error: " << x.what() << std::endl;
            throw x;
        }
    }


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


    /** An Awaitable subclass of a libUV request type, such as uv_fs_t. */
    template <class UV_REQUEST_T>
    class Request : public UV_REQUEST_T, public CoCondition<int> {
    public:

        /// Pass this as the callback to a UV call on this request.
        static void callback(UV_REQUEST_T *req) {
            auto self = static_cast<Request*>(req);
            self->notify(0);
        }

        /// Pass this as the callback to a UV call on this request.
        static void callbackWithStatus(UV_REQUEST_T *req, int status) {
            static_cast<Request*>(req)->notify(status);
        }
    };


    using connect_request = Request<uv_connect_s>;
    using write_request   = Request<uv_write_s>;

}
