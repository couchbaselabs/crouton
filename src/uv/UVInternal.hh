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

    static inline void check(std::signed_integral auto status, const char* what) {
        if (status < 0)
            throw UVError(what, int(status));
    }


    /// Convenience function that returns `Scheduler::current().uvLoop()`.
    uv_loop_s* curLoop();


    /// Closes any type compatible with uv_handle_t. Calls `delete` on the struct pointer
    /// after the close completes.
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


    
    template <class UV_REQUEST_T>
    class RequestWithStatus : public Request<UV_REQUEST_T> {
    public:
        /// Pass this as the callback to a UV call on this request.
        static void callbackWithStatus(UV_REQUEST_T *req, int status) {
            static_cast<RequestWithStatus*>(req)->completed(status);
        }
        static void callback(UV_REQUEST_T *req) = delete;
    };


    using connect_request = RequestWithStatus<uv_connect_s>;
    using write_request   = RequestWithStatus<uv_write_s>;


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
}
