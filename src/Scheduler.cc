//
// Scheduler.cc
//
// 
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
#include "Backtrace.hh"
#include "Coroutine.hh"
#include "UVBase.hh"
#include <iostream>

namespace snej::coro {

    std::string CoroutineName(std::coroutine_handle<> h) {
        struct fake_coroutine_guts {    //FIXME: This is libc++ specific
            void *resume, *destroy;
        };
        auto guts = ((fake_coroutine_guts*)h.address());
        if (!guts)
            return "(null)";
        std::string symbol = fleece::FunctionName(guts->resume ?: guts->destroy);
        if (symbol.ends_with(" (.resume)"))
            symbol = symbol.substr(0, symbol.size() - 10);
        else if (symbol.ends_with(" (.destroy)"))
            symbol = symbol.substr(0, symbol.size() - 11);
        if (symbol.starts_with("snej::coro::"))
            symbol = symbol.substr(12);
        return symbol;
    }

    std::ostream& operator<< (std::ostream& out, std::coroutine_handle<> h) {
        return out << "coro<" << CoroutineName(h) << ">";
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

}
