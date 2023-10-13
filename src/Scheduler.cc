//
// Scheduler.cc
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

#include "Scheduler.hh"
#include "EventLoop.hh"
#include "Internal.hh"
#include "Logging.hh"
#include "Task.hh"

namespace crouton {
    using namespace std;


#pragma mark - SUSPENSION:


    struct Scheduler::SuspensionImpl {
    public:
        SuspensionImpl(coro_handle h, Scheduler *s) 
        :_handle(h), _scheduler(s) { }


        /// Makes the associated suspended coroutine runnable again;
        /// at some point its Scheduler will return it from next().
        /// @note This may be called from any thread, but _only once_.
        /// @warning  The Suspension pointer becomes invalid as soon as this is called.
        void wakeUp() {
            assert(_visible);
            if (_wakeMe.test_and_set() == false) {
                _visible = false;
                LSched->trace("{} unblocked", logCoro{_handle});
                auto sched = _scheduler;
                assert(sched);
                _scheduler = nullptr;
                sched->wakeUp();
            }
        }

        /// Removes the associated coroutine from the suspended set.
        /// You must call this if the coroutine is destroyed while a Suspension exists.
        void cancel() {
            LSched->trace("{} Suspension canceled -- forgetting it", logCoro{_handle});
            assert(_visible);
            _handle = nullptr;
            if (_wakeMe.test_and_set() == false) {
                _visible = false;
                auto sched = _scheduler;
                assert(sched);
                _scheduler = nullptr;
                sched->wakeUp();
            }
        }

        coro_handle         _handle;                    // The coroutine (not really needed)
        Scheduler*          _scheduler;                 // Scheduler that owns coroutine
        std::atomic_flag    _wakeMe = ATOMIC_FLAG_INIT; // Indicates coroutine wants to wake up
        bool                _visible = false;           // Is this Suspension externally visible?
    };


    coro_handle Suspension::handle() const {
        return _impl ? _impl->_handle : coro_handle{};
    }

    void Suspension::wakeUp() {
        if (auto impl = _impl) {
            _impl = nullptr;
            impl->wakeUp();
        }
    }
    
    void Suspension::cancel() {
        if (auto impl = _impl) {
            _impl = nullptr;
            impl->cancel();
        }
    }


#pragma mark - SCHEDULER:
    

    Scheduler& Scheduler::_create() {
        InitLogging();
        assert(!sCurSched);
        sCurSched = new Scheduler();
        LSched->info("Created Scheduler {}", (void*)sCurSched);
        return *sCurSched;
    }

    
    /// True if there are no tasks waiting to run.
    bool Scheduler::isIdle() const {
        if (hasWakers())
            return false;
        else if (_ready.empty())
            return true;
        else if (_ready.size() == 1 && _ready[0] == _eventLoopTask)
            return true;
        return false;
    }

    bool Scheduler::isEmpty() const {
        return isIdle() && _suspended.empty();
    }

    /// Returns true if there are no coroutines ready or suspended, except possibly for the one
    /// belonging to the EventLoop. Checked at the end of unit tests.
    bool Scheduler::assertEmpty() {
        if (auto depth = lifecycle::stackDepth(); depth > 0) {
            LSched->error("** Coroutine stack is non-empty ({})", depth);
        }

        scheduleWakers();
        if (isEmpty() && lifecycle::count() == 0)
            return true;

        LSched->info("Scheduler::assertEmpty: Running event loop until {} ready and {} suspended coroutines finish...",
                     _ready.size(), _suspended.size());
        int attempt = 0;
        const_cast<Scheduler*>(this)->runUntil([&] {
            return (isEmpty() && lifecycle::count() == 0) || ++attempt >= 10;
        });
        if (attempt < 10) {
            LSched->info("...OK, all coroutines finished now.");
            return true;
        }

        LSched->error("** Unexpected coroutines still in existence:");
        lifecycle::logAll();

        LSched->error("** On this Scheduler:");
        for (auto &r : _ready)
            if (r != _eventLoopTask) {
                LSched->info("ready: {}", logCoro{r});
            }
        for (auto &s : _suspended) {
            LSched->info("\tsuspended: {}" , logCoro{s.second._handle});
        }
        return false;
    }


    EventLoop& Scheduler::eventLoop() {
        precondition(isCurrent());
        if (!_eventLoop) {
            _eventLoop = newEventLoop();
            _ownsEventLoop = true;
        }
        return *_eventLoop;
    }

    void Scheduler::useEventLoop(EventLoop* loop) {
        precondition(isCurrent());
        assert(!_eventLoop);
        _eventLoop = loop;
        _ownsEventLoop = false;
    }

    /// Returns a coroutine that runs the event loop, yielding on every iteration.
    Task Scheduler::eventLoopTask() {
        NotReentrant nr(_inEventLoopTask);
        while(true) {
            eventLoop().runOnce(isIdle());    // only block on I/O if no tasks are ready
            if (bool ok = YIELD true; !ok)
                break;
        }
    }

    void Scheduler::run() {
        runUntil([]{return false;});
    }

    void Scheduler::runUntil(std::function<bool()> fn) {
        if (!fn()) {
            if (!_eventLoopTask) {
                _eventLoopTask = eventLoopTask().handle();
                lifecycle::ignoreInCount(_eventLoopTask);
            }
            while (!fn() && !_eventLoopTask.done())
                lifecycle::resume(_eventLoopTask);
            if (_eventLoopTask.done())
                _eventLoopTask = nullptr;
        }
    }

    void Scheduler::_wakeUp() {
        assert(_eventLoop);
        if (isCurrent()) {
            LSched->debug("wake up!");
            if (_eventLoop->isRunning())
                _eventLoop->stop(false);    // efficient stop
        } else {
            LSched->debug("wake up! (from another thread)");
            _eventLoop->stop(true);         // thread-safe stop
        }
    }

    void Scheduler::onEventLoop(std::function<void()> fn) {
        eventLoop().perform(std::move(fn));
    }

    EventLoop::~EventLoop() = default;


    /// Adds a coroutine handle to the end of the ready queue, where at some point it will
    /// be returned from next().
    void Scheduler::schedule(coro_handle h) {
        LSched->debug("schedule {}", logCoro{h});
        precondition(isCurrent());
        assert(!isWaiting(h));
        if (!isReady(h))
            _ready.push_back(h);
    }

    /// Allows a running coroutine `h` to give another ready coroutine some time.
    /// Returns the coroutine that should run next, possibly `h` if no others are ready.
    coro_handle Scheduler::yield(coro_handle h) {
        if (coro_handle nxt = nextOr(nullptr)) {
            schedule(h);
            return nxt;
        } else {
            LSched->debug("yield {} -- continue running", logCoro{h});
            return h;
        }
    }

    void Scheduler::resumed(coro_handle h) {
        precondition(isCurrent());
        if (auto i = std::find(_ready.begin(), _ready.end(), h); i != _ready.end())
            _ready.erase(i);
    }

    /// Returns the coroutine that should be resumed. If none is ready, exits coroutine-land.
    coro_handle Scheduler::next() {
        return nextOr(CORO_NS::noop_coroutine());
    }

    /// Returns the coroutine that should be resumed, or else `dflt`.
    coro_handle Scheduler::nextOr(coro_handle dflt) {
        precondition(isCurrent());
        scheduleWakers();
        if (_ready.empty()) {
            return dflt;
        } else {
            coro_handle h = _ready.front();
            _ready.pop_front();
            LSched->debug("resume {}", logCoro{h});
            return h;
        }
    }

    /// Returns the coroutine that should be resumed,
    /// or else the no-op coroutine that returns to the outer caller.
    coro_handle Scheduler::finished(coro_handle h) {
        LSched->debug("finished {}", logCoro{h});
        precondition(isCurrent());
        assert(h.done());
        assert(!isReady(h));
        assert(!isWaiting(h));
        // Always continue on to the caller of `h`, otherwise things get confused.
        return CORO_NS::noop_coroutine();
    }

    /// Adds a coroutine handle to the suspension set.
    /// To make it runnable again, call the returned Suspension's `wakeUp` method
    /// from any thread.
    Suspension Scheduler::suspend(coro_handle h) {
        LSched->debug("suspend {}", logCoro{h});
        precondition(isCurrent());
        assert(!isReady(h));
        auto [i, added] = _suspended.try_emplace(h.address(), h, this);
        i->second._visible = true;
        return Suspension(&i->second);
    }

    void Scheduler::destroying(coro_handle h) {
        LSched->debug("destroying {}", logCoro{h});
        precondition(isCurrent());
        if (auto i = _suspended.find(h.address()); i != _suspended.end()) {
            SuspensionImpl& sus = i->second;
            if (sus._wakeMe.test_and_set()) {
                // The holder of the Suspension already tried to wake it, so it's OK to delete it:
                _suspended.erase(i);
            } else {
                assert(!sus._visible);
                sus._handle = nullptr;
            }
        }
    }

    /// Called from "normal" code.
    /// Resumes the next ready coroutine and returns true.
    /// If no coroutines are ready, returns false.
    bool Scheduler::resume() {
        if (coro_handle h = nextOr(nullptr)) {
            lifecycle::resume(h);
            return true;
        } else {
            return false;
        }
    }


    bool Scheduler::isReady(coro_handle h) const {
#if 1
        for (auto &x : _ready)
            if (x == h)
                return true;
        return false;
#else // Xcode 14.2 doesn't support this yet:
        return std::ranges::any_of(_ready, [=](auto x) {return x == h;});
#endif
    }

    bool Scheduler::isWaiting(coro_handle h) const {
        return _suspended.find(h.address()) != _suspended.end();
    }

    /// Changes a waiting coroutine's state to 'ready' and notifies the Scheduler to resume
    /// if it's blocked in next(). At some point next() will return this coroutine.
    /// \note  This method is thread-safe.
    void Scheduler::wakeUp() {
        if (_woke.exchange(true) == false)
            _wakeUp();
    }

    bool Scheduler::hasWakers() const {
        if (_woke) {
            for (auto &[h, sus] : _suspended) {
                if (sus._wakeMe.test() && sus._handle)
                    return true;
            }
        }
        return false;
    }

    // Finds any waiting coroutines that want to wake up, removes them from `_suspended`
    // and adds them to `_ready`.
    void Scheduler::scheduleWakers() {
        while (_woke.exchange(false) == true) {
            // Some waiting coroutine is now ready:
            for (auto i = _suspended.begin(); i != _suspended.end();) {
                if (i->second._wakeMe.test()) {
                    if (i->second._handle) {
                        LSched->debug("scheduleWaker({})", logCoro{i->second._handle});
                        _ready.push_back(i->second._handle);
                    } else {
                        LSched->debug("cleaned up canceled Suspension {}", (void*)&i->second);
                    }
                    i = _suspended.erase(i);
                } else {
                    ++i;
                }
            }
        }
    }

}
