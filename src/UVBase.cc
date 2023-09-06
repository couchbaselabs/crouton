//
// UVBase.cc
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

#include "UVBase.hh"
#include "Backtrace.hh"
#include "Task.hh"
#include "UVInternal.hh"
#include <charconv>
#include <cmath>

namespace crouton {
    using namespace std;

    EventLoop* Scheduler::newEventLoop() {
        auto loop = new UVEventLoop();
        loop->ensureWaits();
        return loop;
    }

    UVError::UVError(const char* what, int status)
    :std::runtime_error(what)
    ,err(status)
    { }

    const char* UVError::what() const noexcept {
        if (_message.empty()) {
            const char* errStr;
            switch (err) {
                case UV__EAI_NONAME:    errStr = "unknown host";  break;
                default:                errStr = uv_strerror(err);
            }
            _message = "Error "s + runtime_error::what() + ": " + errStr;
        }
        return _message.c_str();
    }

    
    UVEventLoop::UVEventLoop()
    :_loop(make_unique<uv_loop_t>())
    {
        check(uv_loop_init(_loop.get()), "initializing the event loop");
        _loop->data = this;
    }

    void UVEventLoop::ensureWaits() {
        // Create a timer with an extremely long period,
        // so the event loop always has something to wait on.
        _distantFutureTimer = make_unique<uv_timer_t>();
        uv_timer_init(uvLoop(), _distantFutureTimer.get());
        auto callback = [](uv_timer_t *handle){ };
        uv_timer_start(_distantFutureTimer.get(), callback, 1'000'000'000, 1'000'000'000);
    }

    bool UVEventLoop::_run(int mode)  {
        NotReentrant nr(_running);
//        std::cerr << ">> UVEventLoop (" << (mode==UV_RUN_NOWAIT ? "non" : "") << "blocking"
//        << ", alive=" << (bool)uv_loop_alive(_loop.get()) << ") ...";
        auto ns = uv_hrtime();
        int status = uv_run(_loop.get(), uv_run_mode(mode));
        ns = uv_hrtime() - ns;
//        std::cerr << "... end event loop (" << (ns / 1000000) << "ms, status=" << status << ") <<\n";
        return status != 0;
    }

    void UVEventLoop::run()  {
        _run(UV_RUN_DEFAULT);
    }

    bool UVEventLoop::runOnce(bool waitForIO)  {
        return _run(waitForIO ? UV_RUN_ONCE : UV_RUN_NOWAIT);
    }

    void UVEventLoop::stop()  {
        uv_stop(_loop.get());
    }

    void UVEventLoop::perform(std::function<void()> fn) {
        struct uvAsyncFn : public uv_async_t {
            uvAsyncFn(std::function<void()> &&fn) :_fn(std::move(fn)) { }
            std::function<void()> _fn;
        };

        std::cout << "Scheduler::onEventLoop()\n";
        auto async = new uvAsyncFn(std::move(fn));
        check(uv_async_init(_loop.get(), async, [](uv_async_t *async) noexcept {
            auto self = static_cast<uvAsyncFn*>(async);
            try {
                self->_fn();
            } catch (...) {
                fprintf(stderr, "*** Caught unexpected exception in onEventLoop callback ***\n");
            }
            closeHandle(self);
        }), "making an async call");
        check(uv_async_send(async), "making an async call");
    }


    Future<void> Timer::sleep(double delaySecs) {
        FutureProvider<void> provider;
        Timer::after(delaySecs, [provider]{provider.setResult();});
        return provider;
    }



    uv_loop_s* curLoop() {
        return ((UVEventLoop&)Scheduler::current().eventLoop()).uvLoop();
    }



    static uint64_t ms(double secs){
        return uint64_t(::round(max(secs, 0.0) * 1000.0));
    }


    Timer::Timer(std::function<void()> fn)
    :_fn(std::move(fn))
    ,_handle(new uv_timer_t)
    {
        uv_timer_init(curLoop(), _handle);
        _handle->data = this;
    }

    Timer::~Timer() {
        uv_timer_stop(_handle);
        closeHandle(_handle);
    }

    void Timer::_start(double delaySecs, double repeatSecs) {
        auto callback = [](uv_timer_t *handle) noexcept {
            auto self = (Timer*)handle->data;
            try {
                self->_fn();
            } catch (...) {
                fprintf(stderr, "*** Caught unexpected exception in Timer callback ***\n");
            }
            if (self->_deleteMe)
                delete self;
        };
        uv_timer_start(_handle, callback, ms(delaySecs), ms(repeatSecs));
    }

    void Timer::stop() {
        uv_timer_stop(_handle);
    }

    /*static*/ void Timer::after(double delaySecs, std::function<void()> fn) {
        auto t = new Timer(std::move(fn));
        t->_deleteMe = true;
        t->start(delaySecs);
    }



    struct QueuedWork : public uv_work_t {
        FutureProvider<void>    provider;
        std::function<void()>   fn;
        std::exception_ptr      exception;
    };

    Future<void> OnBackgroundThread(std::function<void()> fn) {
        auto work = new QueuedWork{.fn = std::move(fn)};
        check(uv_queue_work(curLoop(), work, [](uv_work_t *req) noexcept {
            auto work = static_cast<QueuedWork*>(req);
            try {
                work->fn();
            } catch (...) {
                work->exception = std::current_exception();
            }
        }, [](uv_work_t *req, int status) noexcept {
            auto work = static_cast<QueuedWork*>(req);
            if (work->exception)
                work->provider.setResult(work->exception);
            else
                work->provider.setResult();
            delete work;
        }), "making a background call");
        return work->provider.future();
    }

    
    void Randomize(void* buf, size_t len) {
        uv_random_t req;
        check(uv_random(curLoop(), &req, buf, len, 0, nullptr), "generating random bytes");
    }


    std::string CoroutineName(coro_handle h) {
        if (h.address() == nullptr)
            return "(null)";
#ifdef __clang__
        // libc++ specific:
        struct fake_coroutine_guts {
            void *resume, *destroy;
        };
        auto guts = ((fake_coroutine_guts*)h.address());
        std::string symbol = fleece::FunctionName(guts->resume ? guts->resume : guts->destroy);
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

    std::ostream& operator<< (std::ostream& out, coro_handle h) {
        return out << "coro<" << CoroutineName(h) << ">";
    }

}
