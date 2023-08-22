//
// EventLoop.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "Task.hh"
#include <functional>

namespace snej::coro {

    /** Abstract event loop class, used by Scheduler. */
    class EventLoop {
    public:
        EventLoop() = default;

        /// Runs the event loop until there's nothing to wait on, or until `stop` is called.
        virtual void run() =0;

        /// Runs a single cycle of the event loop.
        /// @param waitForIO  If true, the call may block waiting for activity.
        virtual void runOnce(bool waitForIO =true) =0;

        virtual bool isRunning() const {return _running;}

        /// Stops the event loop, causing `run` to return. No-op if not running.
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
