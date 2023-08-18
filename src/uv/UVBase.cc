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


    static const char* messageFor(int err) {
        switch (err) {
            case UV__EAI_NONAME:    return "unknown host";  // default msg is obscure/confusing
            default:                return uv_strerror(err);
        }
    }

    UVError::UVError(int status)
    :std::runtime_error(messageFor(status))
    ,err(status)
    { }


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

    void Timer::start(double delaySecs, double repeatSecs) {
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
}
