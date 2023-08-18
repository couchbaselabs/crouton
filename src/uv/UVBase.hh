//
// UVBase.hh
//
// 
//

#pragma once
#include "Future.hh"
#include <stdexcept>
#include <functional>

struct uv_timer_s;

namespace snej::coro::uv {

    /** Low-level struct pointing to the destination of a read. Binary compatible with uv_buf_t. */
    struct ReadBuf {
        void*   base = nullptr;
        size_t  len = 0;
    };

    /** Low-level struct pointing to the source of a write. Binary compatible with uv_buf_t. */
    struct WriteBuf {
        const void* base = nullptr;
        size_t      len = 0;
    };
    //TODO //FIXME: uv_buf_t's fields are in the opposite order on Windows. Deal with that.


    /** Exception thrown by libuv errors. */
    class UVError : public std::runtime_error {
    public:
        explicit UVError(const char* what, int err);
        const char* what() const noexcept override;

        int err; ///< libuv error code
    private:
        mutable std::string _message;
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

    private:
        void _start(double delaySecs, double repeatSecs);

        std::function<void()>   _fn;
        uv_timer_s*             _handle = nullptr;
        bool                    _deleteMe = false;
    };


    /// Calls the given function on the next iteration of the libuv event loop.
    void OnEventLoop(std::function<void()>);


    /// Calls the given function on a background thread managed by libuv.
    Future<void> OnBackgroundThread(std::function<void()> fn);


    /// Calls the given function on a background thread managed by libuv,
    /// returning its value (or exception) asynchronously.
    template <typename T>
    Future<T> OnBackgroundThread(std::function<T()> fn) {
        T result;
        co_await bgthread([&] {
            result = fn();
        });
        co_return result;
    }
}
