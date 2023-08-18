//
// UVBase.cc
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

#include "UVBase.hh"
#include "UVInternal.hh"
#include "uv.h"

namespace snej::coro::uv {
    using namespace std;


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


    static uint64_t ms(double secs){
        return uint64_t(round(max(secs, 0.0) * 1000.0));
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
        auto callback = [](uv_timer_t *handle) {
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

    void Timer::after(double delaySecs, std::function<void()> fn) {
        auto t = new Timer(std::move(fn));
        t->_deleteMe = true;
        t->start(delaySecs);
    }



    struct onEvtLoop : public uv_async_t {
        onEvtLoop(std::function<void()> &&fn) :_fn(std::move(fn)) { }
        ~onEvtLoop() {uv_close(reinterpret_cast<uv_handle_t*>(this), nullptr);}
        std::function<void()> _fn;
    };

    void OnEventLoop(std::function<void()> fn) {
        auto async = new onEvtLoop(std::move(fn));
        uv_async_init(curLoop(), async, [](uv_async_t *async) {
            auto self = static_cast<onEvtLoop*>(async);
            try {
                self->_fn();
            } catch (...) {
                fprintf(stderr, "*** Caught unexpected exception in OnEventLoop callback ***\n");
            }
            delete self;
        });
    }


    struct QueuedWork : public uv_work_t {
        FutureProvider<void>    provider;
        std::function<void()>   fn;
        std::exception_ptr      exception;
    };

    Future<void> OnBackgroundThread(std::function<void()> fn) {
        auto work = new QueuedWork{.fn = std::move(fn)};
        check(uv_queue_work(curLoop(), work, [](uv_work_t *req) {
            auto work = static_cast<QueuedWork*>(req);
            try {
                work->fn();
            } catch (...) {
                work->exception = std::current_exception();
            }
        }, [](uv_work_t *req, int status) {
            auto work = static_cast<QueuedWork*>(req);
            if (work->exception)
                work->provider.setException(work->exception);
            else
                work->provider.setValue();
            delete work;
        }), "making a background call");
        return work->provider.future();
    }
}
