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
#include <ranges>
#include <unordered_map>

namespace crouton {
    class EventLoop;
    class Suspension;
    class Task;


    /** Schedules coroutines on a single thread. Each thread has an instance of this.
        @warning The API is *not* thread-safe, except as noted. */
    class Scheduler {
    public:
        /// Returns the Scheduler instance for the current thread. (Thread-safe, obviously.)
        static Scheduler& current()                     {return sCurSched ? *sCurSched : _create();}

        /// True if this is the current thread's Scheduler. (Thread-safe.)
        bool isCurrent() const                          {return this == sCurSched;}

        /// True if there are no tasks waiting to run.
        bool isIdle() const;

        bool isEmpty() const;

        /// Returns true if there are no coroutines ready or suspended, except possibly for the one
        /// belonging to the EventLoop. Checked at the end of unit tests.
        bool assertEmpty();

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

        //---- Awaitable: `co_await`ing a Scheduler moves the current coroutine to its thread.
#if 0   //TODO: This doesn't work yet
        class SchedAwaiter  {
        public:
            SchedAwaiter(Scheduler* sched)  :_sched(sched) { }

            bool await_ready() noexcept {return _sched->isCurrent();}

            coro_handle await_suspend(coro_handle h) noexcept   {
                _sched->suspend(h);
                return lifecycle::suspendingTo(h, typeid(*_sched), _sched,
                                               Scheduler::current().next());
            }

            void await_resume() noexcept {
                assert(_sched->isCurrent());
            }
        private:
            Scheduler* _sched;
        };
        SchedAwaiter operator co_await() {return SchedAwaiter(this);}
#endif

        /// Called from "normal" code.
        /// Resumes the next ready coroutine and returns true.
        /// If no coroutines are ready, returns false.
        bool resume();

        //---- Coroutine management; mostly called from coroutine implementations

        /// Adds a coroutine handle to the end of the ready queue, where at some point it will
        /// be returned from next().
        void schedule(coro_handle h);

        /// Allows a running coroutine `h` to give another ready coroutine some time.
        /// Returns the coroutine that should run next, possibly `h` if no others are ready.
        coro_handle yield(coro_handle h);

        /// Removes the coroutine from the ready queue, if it's still in it.
        /// To be called from an `await_resume` method.  //TODO: Is this method still needed?
        void resumed(coro_handle h);

        /// Returns the coroutine that should be resumed. If none is ready, exits coroutine-land.
        coro_handle next();

        /// Returns the coroutine that should be resumed, or else `dflt`.
        coro_handle nextOr(coro_handle dflt);

        /// Returns the coroutine that should be resumed,
        /// or else the no-op coroutine that returns to the outer caller.
        coro_handle finished(coro_handle h);

        /// Adds a coroutine handle to the suspension set.
        /// To make it runnable again, call the returned Suspension's `wakeUp` method
        /// from any thread.
        Suspension suspend(coro_handle h);

        /// Tells the Scheduler a coroutine is about to be destroyed, so it can manage it
        /// correctly if it's in the suspended set.
        void destroying(coro_handle h);

    private:
        struct SuspensionImpl;
        friend class Suspension;
        
        Scheduler() = default;
        static Scheduler& _create();
        EventLoop* newEventLoop();
        coro_handle eventLoopHandle();
        Task eventLoopTask();
        bool isReady(coro_handle h) const;
        bool isWaiting(coro_handle h) const;
        void _wakeUp();
        void wakeUp();
        bool hasWakers() const;
        void scheduleWakers();

        using SuspensionMap = std::unordered_map<const void*,SuspensionImpl>;

        static inline thread_local Scheduler* sCurSched;    // Current thread's instance

        std::deque<coro_handle> _ready;                     // Coroutines that are ready to run
        SuspensionMap           _suspended;                 // Suspended/sleeping coroutines
        EventLoop*              _eventLoop;                 // My event loop
        coro_handle             _eventLoopTask = nullptr;   // EventLoop's coroutine handle
        std::atomic<bool>       _woke = false;              // True if a suspended is waking
        bool                    _ownsEventLoop = false;     // True if I created _eventLoop
        bool                    _inEventLoopTask = false;   // True while in eventLoopTask()
    };



    /** Represents a coroutine that's been suspended by calling Scheduler::suspend().
        It will resume after `wakeUp` is called. */
    class Suspension {
    public:
        /// Default constructor creates an empty/null Suspension.
        Suspension()                    :_impl(nullptr) { }

        Suspension(Suspension&& s)      :_impl(s._impl) {s._impl = nullptr;}
        Suspension& operator=(Suspension&& s) {std::swap(_impl, s._impl); return *this;}
        ~Suspension()                   {if (_impl) cancel();}

        explicit operator bool() const  {return _impl != nullptr;}

        coro_handle handle() const;

        /// Makes the associated suspended coroutine runnable again;
        /// at some point its Scheduler will return it from next().
        /// @note Calling this resets the Suspension to empty.
        /// @note Calling on an empty Suspension is a no-op.
        /// @note This may be called from any thread.
        void wakeUp();

        /// Removes the associated coroutine from the suspended set.
        /// @note Calling this resets the Suspension to empty.
        /// @note Calling on an empty Suspension is a no-op.
        void cancel();

    private:
        friend class Scheduler;
        explicit Suspension(Scheduler::SuspensionImpl* impl) :_impl(impl) { };
        Suspension(Suspension const&) = delete;

        Scheduler::SuspensionImpl* _impl;
    };



    /** General purpose Awaitable to return from `yield_value`.
        It does nothing, just allows the Scheduler to schedule another runnable task if any. */
    struct Yielder : public CORO_NS::suspend_always {
        explicit Yielder(coro_handle myHandle) :_handle(myHandle) { }
        coro_handle await_suspend(coro_handle h) noexcept {
            return lifecycle::yieldingTo(h, Scheduler::current().yield(h));
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
            return lifecycle::finalSuspend(h, Scheduler::current().finished(h));
        }
    };

}
