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
#include "Future.hh"
#include "EventLoop.hh"
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

struct uv_loop_s;
struct uv_timer_s;

namespace crouton {
    class Task;

    /** Exception thrown by libuv errors. */
    class UVError : public std::runtime_error {
    public:
        explicit UVError(const char* what, int err);
        const char* what() const noexcept override;

        int err; ///< libuv error code
    private:
        mutable std::string _message;
    };


    /** Implementation of EventLoop for libuv.*/
    class UVEventLoop final : public EventLoop {
    public:
        UVEventLoop();
        void run() override;
        bool runOnce(bool waitForIO =true) override;
        void stop() override;
        void perform(std::function<void()>) override;

        [[nodiscard]] Future<void> sleep(double delaySecs);

        void ensureWaits();
        uv_loop_s* uvLoop() {return _loop.get();}
    private:
        bool _run(int mode);

        std::unique_ptr<uv_loop_s> _loop;
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
        [[nodiscard]] static Future<void> sleep(double delaySecs);

    private:
        void _start(double delaySecs, double repeatSecs);

        std::function<void()>   _fn;
        uv_timer_s*             _handle = nullptr;
        bool                    _deleteMe = false;
    };


    /// Calls the given function on a background thread managed by libuv.
    [[nodiscard]] Future<void> OnBackgroundThread(std::function<void()> fn);


    /// Calls the given function on a background thread managed by libuv,
    /// returning its value (or exception) asynchronously.
    template <typename T>
    [[nodiscard]] Future<T> OnBackgroundThread(std::function<T()> fn) {
        std::optional<T> result;
        AWAIT OnBackgroundThread([&]() -> void {
            result = fn();
        });
        RETURN std::move(result.value());
    }


    /// Writes cryptographically-secure random bytes to the destination buffer.
    void Randomize(void* buf, size_t len);
}
