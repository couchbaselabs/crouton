//
// Scheduler.hh
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
#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <ranges>
#include <unordered_map>
#include <cassert>

struct uv_loop_s;

namespace crouton {
    class EventLoop;
    class Scheduler;
    class Task;

    static bool kLogScheduler = false;

    /** Represents a coroutine handle that's been suspended by calling Scheduler::suspend(). */
    class Suspension {
    public:

        /// Makes the associated suspended coroutine runnable again;
        /// at some point its Scheduler will return it from next().
        /// \note This may be called from any thread, but _only once_.
        /// \warning  The pointer becomes invalid as soon as this is called.
        void wakeUp();

        // internal only, do not call
        Suspension(coro_handle h, Scheduler *s) :_handle(h), _scheduler(s) { }

    private:
        friend class Scheduler;

        coro_handle         _handle;                    // The coroutine (not really needed)
        Scheduler*          _scheduler;                 // Scheduler that owns coroutine
        std::atomic_flag    _wakeMe = ATOMIC_FLAG_INIT; // Indicates coroutine is awake
    };


    /** Schedules coroutines on a single thread. Each thread has an instance of this.
        @warning The API is *not* thread-safe, except as noted. */
    class Scheduler {
    public:
        /// Returns the Scheduler instance for the current thread. (Thread-safe, obviously.)
        static Scheduler& current() {
            if (!sCurSched)
                sCurSched = new Scheduler();
            return *sCurSched;
        }

        /// True if this is the current thread's Scheduler. (Thread-safe.)
        bool isCurrent() const           {return this == sCurSched;}

        /// True if there are no tasks waiting to run.
        bool isIdle() const {
            return _ready.empty() && !_woke;
        }

        /// Returns true if there are no coroutines ready or suspended, except possibly for the one
        /// belonging to the EventLoop. Checked at the end of unit tests.
        bool assertEmpty() const {
            bool e = true;
            for (auto &r : _ready)
                if (r != _eventLoopTask) {
                    if (kLogScheduler) {std::cerr << "\tready: " << r << std::endl;}
                    e = false;
                }
            for (auto &s : _suspended) {
                if (kLogScheduler) {std::cerr << "\tsuspended: " << s.second._handle << std::endl;}
                e = false;
            }
            return e;
        }

        //---- Event loop:

        /// Returns the associated event loop. If there is none, it creates one.
        EventLoop& eventLoop();

        /// Associates an existing EventLoop instance with this Scheduler/thread.
        void useEventLoop(EventLoop*);

        /// Runs the EventLoop indefinitely, until something calls stop on it.
        void run();

        /// Runs the event loop until the function returns true.
        /// The function is checked before each iteration of the loop.
        void runUntil(std::function<bool()> fn);

        /// Schedules the function to be run at the next iteration of the event loop.
        /// @note  This method is thread-safe.
        void onEventLoop(std::function<void()>);

        /// Runs the lambda/function as soon as possible: either immediately if this Scheduler is
        /// current, else on its next event loop iteration.
        template <std::invocable FN> 
        void asap(FN fn) {
            if (isCurrent()) {
                fn();
            } else {
                onEventLoop(fn);
            }
        }

        //---- Coroutine management; mostly called from coroutine implementations

        /// Adds a coroutine handle to the end of the ready queue, where at some point it will
        /// be returned from next().
        void schedule(coro_handle h) {
            if (kLogScheduler) {std::cerr << "Scheduler::schedule " << h << "\n";}
            assert(isCurrent());
            assert(!isWaiting(h));
            if (!isReady(h))
                _ready.push_back(h);
        }

        /// Allows a running coroutine `h` to give another ready coroutine some time.
        /// Returns the coroutine that should run next, possibly `h` if no others are ready.
        coro_handle yield(coro_handle h) {
            if (coro_handle nxt = nextOr(nullptr)) {
                schedule(h);
                return nxt;
            } else {
                if (kLogScheduler) {std::cerr << "Scheduler::yield " << h << " -- continue running\n";}
                return h;
            }
        }

        void resumed(coro_handle h) {
            assert(isCurrent());
            if (auto i = std::find(_ready.begin(), _ready.end(), h); i != _ready.end())
                _ready.erase(i);
        }

        /// Returns the coroutine that should be resumed. If none is ready, exits coroutine-land.
        coro_handle next() {
            return nextOr(CORO_NS::noop_coroutine());
        }

        /// Returns the coroutine that should be resumed, or else `dflt`.
        coro_handle nextOr(coro_handle dflt) {
            assert(isCurrent());
            scheduleWakers();
            if (_ready.empty()) {
                return dflt;
            } else {
                coro_handle h = _ready.front();
                _ready.pop_front();
                if (kLogScheduler) {std::cerr << "Scheduler::resume " << h << std::endl;}
                return h;
            }
        }

        /// Returns the coroutine that should be resumed,
        /// or else the no-op coroutine that returns to the outer caller.
        coro_handle finished(coro_handle h) {
            if (kLogScheduler) {std::cerr << "Scheduler::finished " << h << "\n";}
            assert(isCurrent());
            assert(h.done());
            assert(!isReady(h));
            assert(!isWaiting(h));
            // Always continue on to the caller of `h`, otherwise things get confused.
            return CORO_NS::noop_coroutine();
        }

        /// Adds a coroutine handle to the suspension set.
        /// To make it runnable again, call the returned Suspension's `wakeUp` method
        /// from any thread.
        Suspension* suspend(coro_handle h) {
            if (kLogScheduler) {std::cerr << "Scheduler::suspend " << h << "\n";}
            assert(isCurrent());
            assert(!isReady(h));
            auto [i, added] = _suspended.try_emplace(h.address(), h, this);
            return &i->second;
        }

        /// Called from "normal" code.
        /// Resumes the next ready coroutine and returns true.
        /// If no coroutines are ready, returns false.
        bool resume() {
            if (coro_handle h = nextOr(nullptr)) {
                h.resume();
                return true;
            } else {
                return false;
            }
        }

    private:
        friend class Suspension;
        
        Scheduler() = default;

        /// Creates an EventLoop. (The implementation creates a UVEventLoop.)
        EventLoop* newEventLoop();

        coro_handle eventLoopHandle();
        Task eventLoopTask();

        bool isReady(coro_handle h) const {
#if 1
            for (auto &x : _ready)
                if (x == h)
                    return true;
            return false;
#else // Xcode 14.2 doesn't support this yet:
            return std::ranges::any_of(_ready, [=](auto x) {return x == h;});
#endif
        }

        bool isWaiting(coro_handle h) const {
            return _suspended.find(h.address()) != _suspended.end();
        }

        void _wakeUp();

        /// Changes a waiting coroutine's state to 'ready' and notifies the Scheduler to resume
        /// if it's blocked in next(). At some point next() will return this coroutine.
        /// \note  This method is thread-safe.
        void wakeUp() {
            if (_woke.exchange(true) == false)
                _wakeUp();
        }

        // Finds any waiting coroutines that want to wake up, removes them from `_suspended`
        // and adds them to `_ready`.
        void scheduleWakers() {
            while (_woke.exchange(false) == true) {
                // Some waiting coroutine is now ready:
                for (auto i = _suspended.begin(); i != _suspended.end();) {
                    if (i->second._wakeMe.test()) {
                        if (kLogScheduler) {std::cerr << "Scheduler::scheduleWaker(" << i->first << ")\n";}
                        _ready.push_back(i->second._handle);
                        i = _suspended.erase(i);
                    } else {
                        ++i;
                    }
                }
            }
        }

        using SuspensionMap = std::unordered_map<const void*,Suspension>;

        static inline __thread Scheduler* sCurSched; // Current thread's instance

        std::deque<coro_handle>  _ready;         // Coroutines that are ready to run
        SuspensionMap       _suspended;     // Suspended/sleeping coroutines
        std::atomic<bool>   _woke = false;  // True if a suspended is waking
        EventLoop*          _eventLoop;     // My event loop
        bool                _ownsEventLoop = false;     // True if I created _eventLoop
        coro_handle              _eventLoopTask = nullptr;   // EventLoop's coroutine handle
        bool                _inEventLoopTask = false;   // True while in eventLoopTask()
    };



    /** General purpose Awaitable to return from `yield_value`.
        It does nothing, just allows the Scheduler to schedule another runnable task if any. */
    struct Yielder : public CORO_NS::suspend_always {
        explicit Yielder(coro_handle myHandle) :_handle(myHandle) { }
        coro_handle await_suspend(coro_handle h) noexcept {
            return Scheduler::current().yield(h);
        }
        void await_resume() const noexcept {
            Scheduler::current().resumed(_handle);
        }
    private:
        coro_handle _handle;
    };

    

    /** General purpose Awaitable to return from `final_suspend`.
        It lets the Scheduler decide which coroutine should run next. */
    struct Finisher : public CORO_NS::suspend_always {
        coro_handle await_suspend(coro_handle h) noexcept {
            return Scheduler::current().finished(h);
        }
    };


}
