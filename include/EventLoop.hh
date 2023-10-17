//
// EventLoop.hh
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

#include <functional>
#include <optional>

namespace crouton {
    template <typename T> class Future;
    class Timer;


    /** Abstract event loop class, owned by a Scheduler.
        Like a Scheduler, an EventLoop is associated with a single thread.
        Concrete implementations of EventLoop use it to check for, and wait for, I/O. */
    class EventLoop {
    public:
        /// Runs the event loop until there's nothing to wait on, or until `stop` is called.
        virtual void run() =0;

        /// Runs a single cycle of the event loop.
        /// @param waitForIO  If true, the call is allowed to block waiting for activity.
        /// @return  True if the event loop wants to run again (I/O or timers waiting)
        virtual bool runOnce(bool waitForIO =true) =0;

        /// True if the event loop is currently in `run` or `runOnce`.
        virtual bool isRunning() const                      {return _running;}

        /// Stops the event loop, causing `run` to return ASAP. No-op if not running.
        /// @note  This method is thread-safe if the `threadSafe` parameter is true.
        virtual void stop(bool threadSafe =true) =0;

        /// Schedules a function to run on the next event loop iteration.
        /// @note  This method is thread-safe.
        virtual void perform(std::function<void()>) =0;

    protected:
        virtual ~EventLoop();
        inline void fireTimer(Timer* t);

        bool _running = false;
    };



    /** A repeating or one-shot timer. */
    class Timer {
    public:
        /// Creates a Timer that will call the given function when it fires.
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

        /// Static method that calls the given function after the given delay.
        static void after(double delaySecs, std::function<void()> fn);

        /// Static method that returns a Future that completes after the given delay.
        staticASYNC<void> sleep(double delaySecs);

    private:
        friend class EventLoop;
        void _start(double delaySecs, double repeatSecs);
        void _fire();

        std::function<void()>   _fn;
        EventLoop*              _eventLoop = nullptr;
        void*                   _impl = nullptr;
        bool                    _deleteMe = false;
    };


    /// Calls the given function on an anonymous background thread.
    ASYNC<void> OnBackgroundThread(std::function<void()> fn);


    /// Calls the given function on an anonymous background thread,
    /// returning its value (or exception) asynchronously.
    template <typename T>
    ASYNC<T> OnBackgroundThread(std::function<T()> fn) {
        std::optional<T> result;
        AWAIT OnBackgroundThread([&]() -> void {
            result = fn();
        });
        RETURN std::move(result.value());
    }


    void EventLoop::fireTimer(Timer* t)    {t->_fire();}

}
