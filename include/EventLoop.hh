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
#include "Base.hh"
#include <functional>

namespace crouton {
    template <typename T> class Future;


    /** Abstract event loop class, used by Scheduler.
        The concrete implementation is UVEventLoop. */
    class EventLoop {
    public:
        EventLoop() = default;

        /// Runs the event loop until there's nothing to wait on, or until `stop` is called.
        virtual void run() =0;

        /// Runs a single cycle of the event loop.
        /// @param waitForIO  If true, the call is allowed to block waiting for activity.
        /// @return  True if the event loop wants to run again (I/O or timers waiting)
        virtual bool runOnce(bool waitForIO =true) =0;

        /// True if the event loop is currently in `run` or `runOnce`.
        virtual bool isRunning() const {return _running;}

        /// Stops the event loop, causing `run` to return ASAP. No-op if not running.
        /// @note  This method is thread-safe.
        virtual void stop() =0;

        /// Schedules a function to run on the next event loop iteration.
        /// @note  This method is thread-safe.
        virtual void perform(std::function<void()>) =0;

    protected:
        virtual ~EventLoop();
        bool _running = false;
    };

}
