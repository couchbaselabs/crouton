//
// CoroLifecycle.hh
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
#include "Base.hh"

// Enable lifecycle tracking in debug builds, by default. Override by defining CROUTON_LIFECYCLES=0
#if !defined(CROUTON_LIFECYCLES) && !defined(NDEBUG)
#define CROUTON_LIFECYCLES 1
#endif

namespace crouton {

    namespace lifecycle {

        // A bunch of hooks to be called at points in the coroutine lifecycle, for logging/debugging

#if CROUTON_LIFECYCLES
        void created(coro_handle, bool ready, std::type_info const& implType);
        void ready(coro_handle);
        void suspendInitial(coro_handle cur);
        coro_handle suspendingTo(coro_handle cur,
                                 std::type_info const& toType, const void* to,
                                 coro_handle next = nullptr);
        coro_handle suspendingTo(coro_handle cur,
                                 coro_handle awaiting,
                                 coro_handle next);
        coro_handle yieldingTo(coro_handle cur, coro_handle next);
        coro_handle finalSuspend(coro_handle cur, coro_handle next);
        void resume(coro_handle);   // calls h.resume()
        void threw(coro_handle);
        void returning(coro_handle);
        void ended(coro_handle);

        void ignoreInCount(coro_handle);
        size_t count();
        size_t stackDepth();

        unsigned getSequence(coro_handle h);

        void logAll();
        void logStacks();
#else
        inline void created(coro_handle, bool ready, std::type_info const& implType) { }
        inline void ready(coro_handle) { }
        inline void suspendInitial(coro_handle cur) { }
        inline coro_handle suspendingTo(coro_handle cur,
                                        std::type_info const& toType, const void* to,
                                        coro_handle next = CORO_NS::noop_coroutine()) {return next;}
        inline coro_handle suspendingTo(coro_handle cur,
                                        coro_handle awaiting,
                                        coro_handle next) {return next;}
        inline coro_handle yieldingTo(coro_handle cur, coro_handle next) {return next;}
        inline coro_handle finalSuspend(coro_handle cur, coro_handle next) {return next;}
        inline void resume(coro_handle h) {h.resume();}
        inline void threw(coro_handle) { }
        inline void returning(coro_handle) { }
        inline void ended(coro_handle) { }

        inline void ignoreInCount(coro_handle) { }
        inline size_t count() {return 0;}
        inline size_t stackDepth() {return 0;}

        inline unsigned getSequence(coro_handle h) {return 0;}

        inline void logAll() { }
        inline void logStacks() { }
#endif
    }


    bool isNoop(coro_handle h);


    /** Returns a description of a coroutine, ideally the name of its function. */
    string CoroutineName(coro_handle);

    /** Writes `CoroutineName(h)` to `out` */
    std::ostream& operator<< (std::ostream& out, coro_handle h);

    /** spdlog won't use `operator<<` to format types in the `std` namespace, and that includes
        std::coroutine_handle<>. Work around this by wrapping it in a trivial custom struct. */
    struct logCoro {
        coro_handle h;
        bool verbose = false;

        friend std::ostream& operator<<(std::ostream& out, logCoro lc);
    };

}

