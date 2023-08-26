//
// Scheduler.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "Scheduler.hh"
#include "Backtrace.hh"
#include "Coroutine.hh"
#include "EventLoop.hh"
#include <charconv>
#include <iostream>

namespace crouton {

    std::string CoroutineName(std::coroutine_handle<> h) {
        if (h.address() == nullptr)
            return "(null)";
#ifdef __clang__
        // libc++ specific:
        struct fake_coroutine_guts {
            void *resume, *destroy;
        };
        auto guts = ((fake_coroutine_guts*)h.address());
        std::string symbol = fleece::FunctionName(guts->resume ?: guts->destroy);
        if (symbol.ends_with(" (.resume)"))
            symbol = symbol.substr(0, symbol.size() - 10);
        else if (symbol.ends_with(" (.destroy)"))
            symbol = symbol.substr(0, symbol.size() - 11);
        if (symbol.starts_with("crouton::"))
            symbol = symbol.substr(9);
        return symbol;
#else
        char buf[20];
        auto result = std::to_chars(&buf[0], &buf[20], intptr_t(h.address()), 16);
        return std::string(&buf[0], result.ptr);
#endif
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
