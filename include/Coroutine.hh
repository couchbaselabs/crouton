//
// Coroutine.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include <coroutine>
#include <exception>
#include <cassert>
#include <iosfwd>
#include <stdexcept>
#include <iostream>//TEMP


// Synonyms for coroutine primitives. Optional, but they're more visible in the code.
#define AWAIT  co_await
#define YIELD  co_yield
#define RETURN co_return

namespace crouton {
    template <class INSTANCE, class SELF>
    class CoroutineImpl;


    /** Base class for a coroutine handle, the public object returned by a coroutine function. */
    template <class IMPL>
    class CoroutineHandle {
    public:
        // movable, but not copyable.
        CoroutineHandle(CoroutineHandle&& c)        :_handle(c._handle) {c._handle = {};}
        ~CoroutineHandle()                          {if (_handle) _handle.destroy();}

        using promise_type = IMPL;                  // The name `promise_type` is magic here
        IMPL& impl()                                {return _handle.promise();}

    protected:
        using handle_type = std::coroutine_handle<IMPL>;

        CoroutineHandle() = default;
        explicit CoroutineHandle(handle_type h)     :_handle(h) {}
        handle_type handle()                        {return _handle;}
        void setHandle(handle_type h)               {_handle = h;}
    private:
        friend class CoroutineImpl<CoroutineHandle<IMPL>,IMPL>;
        
        handle_type _handle;    // The internal coroutine handle
    };



    /** Base class for a coroutine implementation or "promise_type". */
    template <class INSTANCE, class SELF>
    class CoroutineImpl {
    public:
        using handle_type = std::coroutine_handle<SELF>;

        CoroutineImpl() = default;

        handle_type handle()                    {return handle_type::from_promise((SELF&)*this);}

        //---- C++ coroutine internal API:

        // unfortunately the compiler doesn't like this; subclass must implement it instead.
        //INSTANCE get_return_object()            {return INSTANCE(handle());}

        // Determines whether the coroutine starts suspended when created, or runs immediately.
        std::suspend_always initial_suspend()   {
            //std::cerr << "New " << typeid(SELF).name() << " " << handle() << std::endl;
            return {};
        }

        // Invoked if the coroutine throws an exception
        void unhandled_exception()              {_exception = std::current_exception();}

        // Invoked after the coroutine terminates for any reason.
        // "You must not return a value that causes the terminated coroutine to try to continue
        // running! The only useful thing you might do in this method other than returning straight
        // to the  caller is to transfer control to a different suspended coroutine."
        std::suspend_always final_suspend() noexcept { return {}; }

        // Other important methods for subclasses:
        // XXX yield_value(YYY value) { ... }
        // void return_value(XXX value) { ... }
        // void return_void() { ... }

    protected:
        CoroutineImpl(CoroutineImpl&) = delete;
        CoroutineImpl(CoroutineImpl&&) = delete;

        void clear()    {_exception = nullptr;}
        void rethrow()  {if (auto x = _exception) {clear(); std::rethrow_exception(x);}}

    private:
        std::exception_ptr  _exception = nullptr;             // Latest exception thrown
    };



    /** General purpose awaiter that manages control flow during `co_yield`.
        It arranges for a specific 'consumer' coroutine, given in the constructor,
        to be resumed by the `co_yield` call. It can be `std::noop_coroutine()` to instead resume
        the outer non-coro code that called `resume`. */
    class YielderTo {
    public:
        /// Arranges for `consumer` to be returned from `await_suspend`, making it the next
        /// coroutine to run after the `co_yield`.
        explicit YielderTo(std::coroutine_handle<> consumer) : _consumer(consumer) {}

        /// Arranges for the outer non-coro caller to be resumed after the `co_yield`.
        explicit YielderTo() :YielderTo(std::noop_coroutine()) { }

        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {return _consumer;}

        void await_resume() noexcept {}

    private:
        std::coroutine_handle<> _consumer; // The coroutine that's awaiting my result, or null if none.
    };


    /** A utility to detect re-entrant use of a coroutine method, i.e. calling it again before the
        first call completes. In some cases this is illegal because it would mess up its state.

        `NotReentrant` simply sets the flag `scope` when constructed, and clears it when destructed.
        An instance would be declared at the start of a non-reentrant method.
        `scope` would typically be a data member of `this`. */
    class NotReentrant {
    public:
        explicit NotReentrant(bool& scope) 
        :_scope(scope)
        {
            if (_scope) throw std::logic_error("Illegal reentrant call");
            _scope = true;
        }

        ~NotReentrant() {_scope = false;}

    private:
        bool& _scope;
    };


    /** Returns a description of a coroutine, ideally the name of its function. */
    std::string CoroutineName(std::coroutine_handle<>);

    /** Writes `CoroutineName(h)` to `out` */
    std::ostream& operator<< (std::ostream& out, std::coroutine_handle<> h);

}
