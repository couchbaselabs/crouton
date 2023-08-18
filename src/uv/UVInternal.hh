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
#include <coroutine>
#include <stdexcept>

namespace snej::coro::uv {

    static inline void check(std::signed_integral auto status) {
        if (status < 0)
            throw UVError(int(status));
    }


    /// Convenience function that returns `Scheduler::current().uvLoop()`.
    uv_loop_s* curLoop();


    template <class T>
    void closeHandle(T* &handle) {
        if (handle) {
            handle->data = nullptr;
            uv_close((uv_handle_t*)handle, [](uv_handle_t* h) {delete (T*)h;});
            handle = nullptr;
        }
    }


    /// An Awaitable subclass of a libUV request type, such as uv_fs_t.
    template <class UV_REQUEST_T>
    class Request : public UV_REQUEST_T {
    public:

        /// Pass this as the callback to a UV call on this request.
        static void callback(UV_REQUEST_T *req) {
            auto self = static_cast<Request*>(req);
            self->completed();
        }

        // Coroutine awaiter methods:
        bool await_ready()      {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coro) noexcept {
            _suspension = Scheduler::current().suspend(coro);
            return Scheduler::current().next();
        }
        int await_resume()  {return _status;}

    protected:
        void completed() {
            _called = true;
            assert(_suspension);
            _suspension->wakeUp();
        }
        bool        _called = false;
        int         _status = -1;

    private:
        Suspension* _suspension = nullptr;
    };


    template <class UV_REQUEST_T>
    class RequestWithStatus : public Request<UV_REQUEST_T> {
    public:
        static void callbackWithStatus(UV_REQUEST_T *req, int status) {
            auto self = static_cast<RequestWithStatus*>(req);
            self->_status = status;
            self->completed();
        }
    };


    class fs_request : public Request<uv_fs_s> {
    public:
        ~fs_request()       {if (_called) uv_fs_req_cleanup(this);}
        int await_resume()  {return int(result);}
    private:
    };


    using connect_request = RequestWithStatus<uv_connect_s>;

    using write_request = RequestWithStatus<uv_write_s>;
}
