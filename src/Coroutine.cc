//
// Coroutine.cc
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

#include "Coroutine.hh"
#include "Memoized.hh"
#include "Logging.hh"
#include "Scheduler.hh"

#include <charconv>
#include <iostream>

namespace crouton {
    using namespace std;


#pragma mark - COMUTEX:


    coro_handle CoMutex::await_suspend(coro_handle h) noexcept {
        auto& sched = Scheduler::current();
        _waiters.emplace_back(sched.suspend(h));
        return lifecycle::suspendingTo(h, CRTN_TYPEID(*this), this, sched.next());
    }

    void CoMutex::unlock() {
        if (_waiters.empty()) {
            _locked = false;
        } else {
            Suspension next = std::move(_waiters.front());
            _waiters.erase(_waiters.begin());
            next.wakeUp();
        }
    }


    bool isNoop(coro_handle h) {
        //FIXME: This works with libc++, but is not guaranteed to work in all C++ runtimes. "Return values from different calls to noop_coroutine may and may not compare equal."
        static auto nop = CORO_NS::noop_coroutine();
        return h == nullptr || h == nop;
    }


    string CoroutineName(coro_handle h) {
        if (h.address() == nullptr)
            return "(null)";
#ifdef __clang__
        // libc++ specific:
        struct fake_coroutine_guts {
            void *resume, *destroy;
        };
        auto guts = ((fake_coroutine_guts*)h.address());
        return GetFunctionName(guts->resume ? guts->resume : guts->destroy);
#else
        char buf[20];
        auto result = std::to_chars(&buf[0], &buf[20], intptr_t(h.address()), 16);
        return string(&buf[0], result.ptr);
#endif
    }


    std::ostream& operator<< (std::ostream& out, coro_handle h) {
        if (!h)
            return out << "¢null";
        else if (isNoop(h))
            return out << "¢exit";
        else
            return out << "¢" << lifecycle::getSequence(h);
    }

    std::ostream& operator<<(std::ostream& out, logCoro lc) {
        if (lc.verbose)
            return out << CoroutineName(lc.h);
        else
            return out << lc.h;
    }

}
