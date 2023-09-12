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
#include <optional>
#include <ranges>
#include <unordered_map>
#include <cassert>

#include "Logging.hh"//TEMP

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

        /// Returns true if there are no coroutines ready or suspended, except possibly for the one
        /// belonging to the EventLoop. Checked at the end of unit tests.
        bool assertEmpty() const;

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
        Suspension* suspend(coro_handle h);

        /// Tells the Scheduler a coroutine is about to be destroyed, so it can manage it
        /// correctly if it's in the suspended set.
        void destroying(coro_handle h);

    private:
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
        void scheduleWakers();

        using SuspensionMap = std::unordered_map<const void*,Suspension>;

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
        /// Makes the associated suspended coroutine runnable again;
        /// at some point its Scheduler will return it from next().
        /// @note This may be called from any thread, but _only once_.
        /// @warning  The Suspension pointer becomes invalid as soon as this is called.
        void wakeUp();

        // internal only, do not call
        Suspension(coro_handle h, Scheduler *s) :_handle(h), _scheduler(s) { }

    private:
        friend class Scheduler;

        coro_handle         _handle;                    // The coroutine (not really needed)
        Scheduler*          _scheduler;                 // Scheduler that owns coroutine
        std::atomic_flag    _wakeMe = ATOMIC_FLAG_INIT; // Indicates coroutine wants to wake up
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


#pragma mark - BLOCKER:

    
    /** A simple way to await a future event. A coroutine that `co_await`s a Blocker will block
        until something calls the Blocker's `notify` method. This provides an easy way to turn
        a completion-callback based API into a coroutine-based one: create a Blocker, start
        the operation, then `co_await` the Blocker. In the completion callback, call `notify`.

        The value you pass to `notify` will be returned from the `co_await`.

        Blocker supports only one waiting coroutine. If you need more, use a CoCondition. */
    template <typename T>
    class Blocker {
    public:
        bool await_ready() noexcept {
            return _value.has_value();
        }

        coro_handle await_suspend(coro_handle h) noexcept {
            _suspension = Scheduler::current().suspend(h);
            return lifecycle::suspendingTo(h, typeid(*this), this);
        }

        T&& await_resume() noexcept {
            return std::move(_value).value();
        }

        template <typename U>
        void notify(U&& val) {
            _value.emplace(std::forward<U>(val));
            if (auto s = _suspension) {
                _suspension = nullptr;
                s->wakeUp();
            }
        }

    protected:
        T const& value() const  {return _value.value();}

    private:
        std::optional<T> _value;
        Suspension* _suspension = nullptr;
    };


#pragma mark - CO-CONDITION:


    /** A cooperative condition variable. A coroutine that `co_await`s it will block until
        something calls `notify` or `notifyAll`. If there are multiple coroutines blocked,
        `notify` wakes one, while `notifyAll` wakes all of them.
        @warning  Not thread-safe, despite the name! */
    class CoCondition {
    public:
        struct awaiter;

        awaiter operator co_await() {
            return awaiter{{}, this};
        }

        void notifyOne() {
            if (!_awaiters.empty()) {
                awaiter* a = _awaiters.front();
                _awaiters.erase(_awaiters.begin());
                LSched->debug("CoCondition {}: waking suspension={}",
                              (void*)this, (void*)a->_suspension);
                a->wakeUp();
            }
        }
        void notifyAll() {
            auto awaiters = std::move(_awaiters);
            for (auto a : awaiters) {
                LSched->debug("CoCondition {}: waking suspension={}",
                              (void*)this, (void*)a->_suspension);
                a->wakeUp();
            }
        }

        struct awaiter : CORO_NS::suspend_always {
            CoCondition* _cond;
            Suspension*  _suspension;

            coro_handle await_suspend(coro_handle h) noexcept {
                _suspension = Scheduler::current().suspend(h);
                LSched->debug("CoCondition {}: adding awaiter with h={}, suspension={}",
                              (void*)this, logCoro{h}, (void*)_suspension);
                _cond->_awaiters.push_back(this);
                return lifecycle::suspendingTo(h, typeid(*_cond), _cond);
            }

            ~awaiter() {
                assert(!_suspension);
                if (_suspension)
                    _cond->remove(this);
            }
        private:
            friend class CoCondition;

            void wakeUp() {
                auto sus = _suspension;
                _suspension = nullptr;
                LSched->debug("CoCondition {}: waking suspension={}",
                              (void*)this, (void*)sus);
                sus->wakeUp();
            }
        };

        ~CoCondition() {assert(_awaiters.empty());}

    private:
        void remove(awaiter* a) {
            if (auto i = find(_awaiters.begin(), _awaiters.end(), a); i != _awaiters.end())
                _awaiters.erase(i);
        }

        //TODO: It would be more efficient to make awaiter a doubly-linked list element.
        std::vector<awaiter*> _awaiters;
    };

}
