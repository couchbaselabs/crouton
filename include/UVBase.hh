//
// UVBase.hh
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
#include "Error.hh"
#include "Future.hh"
#include "EventLoop.hh"
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

struct uv_async_s;
struct uv_loop_s;
struct uv_timer_s;

namespace crouton {
    class Task;

    /** Enum for using libuv errors with Error. */
    enum class UVError : errorcode_t { };

    template <> struct ErrorDomainInfo<UVError> {
        static constexpr string_view name = "libuv";
        static string description(errorcode_t);
    };


    /** Implementation of EventLoop for libuv.*/
    class UVEventLoop final : public EventLoop {
    public:
        UVEventLoop();
        void run() override;
        bool runOnce(bool waitForIO =true) override;
        void stop(bool threadSafe) override;
        void perform(std::function<void()>) override;

        ASYNC<void> sleep(double delaySecs);

        void ensureWaits();
        uv_loop_s* uvLoop() {return _loop.get();}
    private:
        bool _run(int mode);

        std::unique_ptr<uv_loop_s> _loop;
        std::unique_ptr<uv_async_s> _async;
        std::unique_ptr<uv_timer_s> _distantFutureTimer;
    };


    /** A repeating or one-shot timer. */
    class Timer {
    public:
        Timer(std::function<void()> fn);
        ~Timer();

        /// Calls the function once after a delay.
        void once(double delaySecs)                         {_start(delaySecs, 0);}

        /// Calls the function repeatedly.
        void start(double intervalSecs)                     {_start(intervalSecs, intervalSecs);}

        /// Calls the function repeatedly after a delay.
        void start(double delaySecs, double intervalSecs)   {_start(delaySecs, intervalSecs);}

        /// Stops any future calls. The timer's destruction also stops calls.
        void stop();

        /// Static method, that calls the given function after the given delay.
        static void after(double delaySecs, std::function<void()> fn);

        /// Returns a Future that completes after the given delay.
        staticASYNC<void> sleep(double delaySecs);

    private:
        void _start(double delaySecs, double repeatSecs);

        std::function<void()>   _fn;
        uv_timer_s*             _handle = nullptr;
        bool                    _deleteMe = false;
    };


    /// Calls the given function on a background thread managed by libuv.
    ASYNC<void> OnBackgroundThread(std::function<void()> fn);


    /// Calls the given function on a background thread managed by libuv,
    /// returning its value (or exception) asynchronously.
    template <typename T>
    ASYNC<T> OnBackgroundThread(std::function<T()> fn) {
        std::optional<T> result;
        AWAIT OnBackgroundThread([&]() -> void {
            result = fn();
        });
        RETURN std::move(result.value());
    }


    /// Writes cryptographically-secure random bytes to the destination buffer.
    void Randomize(void* buf, size_t len);
}
