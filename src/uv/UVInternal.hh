//
// UVInternal.hh
//
// 
//

#pragma once
#include "UVBase.hh"
#include "Scheduler.hh"
#include "uv.h"
#include <concepts>
#include <stdexcept>

namespace snej::coro::uv {

    /// Checks a libuv function result and throws a UVError exception if it's negative.
    static inline void check(std::signed_integral auto status, const char* what) {
        if (status < 0)
            throw UVError(what, int(status));
    }


    /// Creates a std::exception_ptr from an exception object.
    /// (Unfortunately it has to throw and catch it to do so.)
    template <typename X>
    std::exception_ptr makeExceptionPtr(X const& exc) {
        try {
            throw exc;
        } catch (...) {
            return std::current_exception();
        }
        //abort(); // unreachable
    }


    /// Convenience function that returns `Scheduler::current().uvLoop()`.
    uv_loop_s* curLoop();


    /// Closes any type compatible with `uv_handle_t`, and
    /// calls `delete` on the struct pointer after the close completes.
    template <class T>
    void closeHandle(T* &handle) {
        if (handle) {
            handle->data = nullptr;
            uv_close((uv_handle_t*)handle, [](uv_handle_t* h) noexcept {
                delete (T*)h;
            });
            handle = nullptr;
        }
    }


    /** An Awaitable subclass of a libUV request type, such as uv_fs_t. */
    template <class UV_REQUEST_T>
    class Request : public UV_REQUEST_T {
    public:

        /// Pass this as the callback to a UV call on this request.
        static void callback(UV_REQUEST_T *req) {
            auto self = static_cast<Request*>(req);
            self->completed(0);
        }

        /// Pass this as the callback to a UV call on this request.
        static void callbackWithStatus(UV_REQUEST_T *req, int status) {
            static_cast<Request*>(req)->completed(status);
        }

        // Coroutine awaiter methods:
        bool await_ready()      {return _status.has_value();}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coro) noexcept {
            _suspension = Scheduler::current().suspend(coro);
            return Scheduler::current().next();
        }
        [[nodiscard]] int await_resume()      {return _status.value();}

    protected:
        void completed(int status) {
            if (status < 0)
                std::cerr << "ERROR " << status << " in callback\n";
            _status = status;
            if (_suspension)
                _suspension->wakeUp();
        }
        std::optional<int> _status;

    private:
        Suspension* _suspension = nullptr;
    };


    using connect_request = Request<uv_connect_s>;
    using write_request   = Request<uv_write_s>;

}
