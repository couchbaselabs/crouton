//
// EventLoop.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
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
        virtual void runOnce(bool waitForIO =true) =0;

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
