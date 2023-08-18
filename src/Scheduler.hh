//
// Scheduler.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include <atomic>
#include <coroutine>
#include <deque>
#include <mutex>
#include <ranges>
#include <unordered_set>
#include <cassert>

struct uv_loop_s;

namespace snej::coro {
    class Scheduler;


    /// Represents a coroutine handle that's been suspended by calling Scheduler::suspend().
    class Suspension {
    public:

        /// Makes the associated suspended coroutine runnable again; at some point the
        /// Scheduler will return it from next().
        /// \note This may be called from any thread, but only once.
        /// \warning  The pointer becomes invalid as soon as this is called.
        void wakeUp();

        // internal only, do not call
        Suspension(std::coroutine_handle<> h, Scheduler *s) :_handle(h), _scheduler(s) { }

    private:
        friend class Scheduler;

        std::coroutine_handle<> _handle;                    // The coroutine (not really needed)
        Scheduler*              _scheduler;                 // Scheduler that owns coroutine
        std::atomic_flag        _wakeMe = ATOMIC_FLAG_INIT; // Indicates coroutine is awake
    };


    // Schedules coroutines on a single thread. Each thread has an instance of this.
    class Scheduler {
    public:
        using handle = std::coroutine_handle<>;

        /// Returns the Scheduler instance for the current thread.
        static Scheduler& current() {
            if (!sCurSched)
                sCurSched = new Scheduler();
            return *sCurSched;
        }

        /// True if this is the current thread's Scheduler.
        bool isCurrent()            {return this == sCurSched;}


        /// Adds a coroutine handle to the end of the ready queue, where at some point it will
        /// be returned from next().
        void schedule(handle h) {
            assert(isCurrent());
            assert(!isReady(h));
            assert(!isWaiting(h));
            _ready.push_back(h);
        }

        /// Returns the first coroutine handle that's ready to resume. If none is ready, blocks.
        handle next() {
            assert(isCurrent());
            while (true) {
                if (auto w = nextIfAny())
                    return w;
                _wait();
            }
        }

        void _wait();
        void _wakeUp();

        /// Returns the first coroutine handle that's ready to resume, or else nullptr.
        handle nextIfAny() {
            scheduleWakers();
            if (_ready.empty()) {
                return nullptr;
            } else {
                handle h = _ready.front();
                _ready.pop_front();
                return h;
            }
        }

        /// Adds a coroutine handle to the suspension set.
        /// To make it runnable again, call the returned Suspension's `wakeUp` method once
        /// from any thread.
        Suspension* suspend(handle h) {
            assert(isCurrent());
            assert(!isReady(h));
            auto [i, added] = _suspended.try_emplace(h, h, this);
            return &i->second;
        }

        //---- libuv additions:

        /// Returns the associated libuv event loop. If there is none, it creates one.
        uv_loop_s* uvLoop();

        /// Associates an existing libuv event loop with this Scheduler/thread.
        /// Must be called before the first call to `uvLoop`.
        void useUVLoop(uv_loop_s*);

    private:
        friend class Suspension;
        
        Scheduler() = default;

        bool isReady(handle h) const {
            return std::ranges::any_of(_ready, [=](auto x) {return x == h;});
        }

        bool isWaiting(handle h) const {
            return _suspended.find(h) != _suspended.end();
        }

        /// Changes a waiting coroutine's state to 'ready' and notifies the Scheduler to resume
        /// if it's blocked in next(). At some point next() will return this coroutine.
        /// \note  This method is thread-safe.
        void wakeUp() {
            std::unique_lock<std::mutex> lock(_mutex);
            _woke = true;
            _wakeUp();
        }

        // Finds any waiting coroutines that want to wake up, removes them from `_waiting`
        // and adds them to `_ready`.
        void scheduleWakers() {
            while (_woke.exchange(false)) {
                // Some waiting coroutine is now ready:
                for (auto i = _suspended.begin(); i != _suspended.end();) {
                    if (i->second._wakeMe.test()) {
                        _ready.push_back(i->first);
                        i = _suspended.erase(i);
                    } else {
                        ++i;
                    }
                }
            }
        }

        static inline __thread Scheduler* sCurSched;

        std::deque<handle>                      _ready;         // Coroutines that are ready to run
        std::unordered_map<handle,Suspension>   _suspended;     // Suspended/sleeping coroutines
        std::atomic<bool>                       _woke = false;  // True if a suspended is waking
        std::mutex                              _mutex;         // Synchronizes _cond
        std::condition_variable                 _cond;          // Notifies next() of coro waking
        uv_loop_s*                              _uvloop = nullptr; // libuv event loop
    };

}
