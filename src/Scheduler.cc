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
#include "Coroutine.hh"
#include "EventLoop.hh"
#include "Task.hh"
#include <charconv>
#include <iostream>

namespace crouton {


    Scheduler& Scheduler::_create() {
        assert(!sCurSched);
        sCurSched = new Scheduler();
        return *sCurSched;
    }

    
    /// True if there are no tasks waiting to run.
    bool Scheduler::isIdle() const {
        return _ready.empty() && !_woke;
    }

    /// Returns true if there are no coroutines ready or suspended, except possibly for the one
    /// belonging to the EventLoop. Checked at the end of unit tests.
    bool Scheduler::assertEmpty() const {
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


    void Suspension::wakeUp() {
        if (_wakeMe.test_and_set() == false) {
            auto sched = _scheduler;
            assert(sched);
            _scheduler = nullptr;
            sched->wakeUp();
        }
    }

    EventLoop& Scheduler::eventLoop() {
        assert(isCurrent());
        if (!_eventLoop) {
            _eventLoop = newEventLoop();
            _ownsEventLoop = true;
        }
        return *_eventLoop;
    }

    void Scheduler::useEventLoop(EventLoop* loop) {
        assert(isCurrent());
        assert(!_eventLoop);
        _eventLoop = loop;
        _ownsEventLoop = false;
    }

    /// Returns a coroutine that runs the event loop, yielding on every iteration.
    Task Scheduler::eventLoopTask() {
        NotReentrant nr(_inEventLoopTask);
        while(true) {
            eventLoop().runOnce(isIdle());    // only block on I/O if no tasks are ready
            YIELD true;
        }
    }

    void Scheduler::run() {
        runUntil([]{return false;});
    }

    void Scheduler::runUntil(std::function<bool()> fn) {
        if (!fn()) {
            if (!_eventLoopTask)
                _eventLoopTask = eventLoopTask().handle();
            while (!fn() && !_eventLoopTask.done())
                _eventLoopTask.resume();
            if (_eventLoopTask.done())
                _eventLoopTask = nullptr;
        }
    }

    void Scheduler::_wakeUp() {
        assert(_eventLoop);
        if (kLogScheduler) {std::cerr << "\twake up!\n";}
        if (isCurrent()) {
            if (_eventLoop->isRunning())
                _eventLoop->stop();
        } else {
            // Stopping the event loop from another thread is tricky since most of libuv is not
            // thread-safe.
            _eventLoop->perform([loop=_eventLoop] {
                loop->stop();
            });
        }
    }

    void Scheduler::onEventLoop(std::function<void()> fn) {
        eventLoop().perform(std::move(fn));
    }

    EventLoop::~EventLoop() = default;


    /// Adds a coroutine handle to the end of the ready queue, where at some point it will
    /// be returned from next().
    void Scheduler::schedule(coro_handle h) {
        if (kLogScheduler) {std::cerr << "Scheduler::schedule " << h << "\n";}
        assert(isCurrent());
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
            if (kLogScheduler) {std::cerr << "Scheduler::yield " << h << " -- continue running\n";}
            return h;
        }
    }

    void Scheduler::resumed(coro_handle h) {
        assert(isCurrent());
        if (auto i = std::find(_ready.begin(), _ready.end(), h); i != _ready.end())
            _ready.erase(i);
    }

    /// Returns the coroutine that should be resumed. If none is ready, exits coroutine-land.
    coro_handle Scheduler::next() {
        return nextOr(CORO_NS::noop_coroutine());
    }

    /// Returns the coroutine that should be resumed, or else `dflt`.
    coro_handle Scheduler::nextOr(coro_handle dflt) {
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
    coro_handle Scheduler::finished(coro_handle h) {
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
    Suspension* Scheduler::suspend(coro_handle h) {
        if (kLogScheduler) {std::cerr << "Scheduler::suspend " << h << "\n";}
        assert(isCurrent());
        assert(!isReady(h));
        auto [i, added] = _suspended.try_emplace(h.address(), h, this);
        return &i->second;
    }

    /// Called from "normal" code.
    /// Resumes the next ready coroutine and returns true.
    /// If no coroutines are ready, returns false.
    bool Scheduler::resume() {
        if (coro_handle h = nextOr(nullptr)) {
            h.resume();
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

    // Finds any waiting coroutines that want to wake up, removes them from `_suspended`
    // and adds them to `_ready`.
    void Scheduler::scheduleWakers() {
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

}
