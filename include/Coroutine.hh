//
// Coroutine.hh
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
#include <exception>
#include <cassert>
#include <iosfwd>
#include <stdexcept>
#include <iostream>//TEMP

#if defined(__has_include)
#   if __has_include(<coroutine>)
#      include <coroutine>
#      define CORO_NS std
#   elif __has_include(<experimental/coroutine>)
#      include <experimental/coroutine>
#      define CORO_NS std::experimental
#   else
#      error No coroutine header
#   endif
#else
#   include <coroutine>
#   define CORO_NS std
#endif


// `is_type_complete_v<T>` evaluates to true iff T is a complete (fully-defined) type.
// By Raymond Chen: https://devblogs.microsoft.com/oldnewthing/20190710-00/?p=102678
template<typename, typename = void>
    constexpr bool is_type_complete_v = false;
template<typename T>
    constexpr bool is_type_complete_v<T, std::void_t<decltype(sizeof(T))>> = true;

// `NonReference` is a concept that applies to any value that's not a reference.
template <typename T>
    concept NonReference = !std::is_reference_v<T>;


// Synonyms for coroutine primitives. Optional, but they're more visible in the code.
#define AWAIT  co_await
#define YIELD  co_yield
#define RETURN co_return

namespace crouton {
    template <class INSTANCE, class SELF>
    class CoroutineImpl;

    using coro_handle = CORO_NS::coroutine_handle<>;


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
        using handle_type = CORO_NS::coroutine_handle<IMPL>;

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
        using handle_type = CORO_NS::coroutine_handle<SELF>;

        CoroutineImpl() = default;

        handle_type handle()                    {return handle_type::from_promise((SELF&)*this);}

        //---- C++ coroutine internal API:

        // unfortunately the compiler doesn't like this; subclass must implement it instead.
        //INSTANCE get_return_object()            {return INSTANCE(handle());}

        // Determines whether the coroutine starts suspended when created, or runs immediately.
        CORO_NS::suspend_always initial_suspend()   {
            //std::cerr << "New " << typeid(SELF).name() << " " << handle() << std::endl;
            return {};
        }

        // Invoked after the coroutine terminates for any reason.
        // "You must not return a value that causes the terminated coroutine to try to continue
        // running! The only useful thing you might do in this method other than returning straight
        // to the  caller is to transfer control to a different suspended coroutine."
        CORO_NS::suspend_always final_suspend() noexcept { return {}; }

        // Other important methods for subclasses:
        // XXX yield_value(YYY value) { ... }
        // void return_value(XXX value) { ... }
        // void return_void() { ... }

    protected:
        CoroutineImpl(CoroutineImpl&) = delete;
        CoroutineImpl(CoroutineImpl&&) = delete;
    };



    /** General purpose awaiter that manages control flow during `co_yield`.
        It arranges for a specific 'consumer' coroutine, given in the constructor,
        to be resumed by the `co_yield` call. It can be `CORO_NS::noop_coroutine()` to instead resume
        the outer non-coro code that called `resume`. */
    class YielderTo {
    public:
        /// Arranges for `consumer` to be returned from `await_suspend`, making it the next
        /// coroutine to run after the `co_yield`.
        explicit YielderTo(coro_handle consumer) : _consumer(consumer) {}

        /// Arranges for the outer non-coro caller to be resumed after the `co_yield`.
        explicit YielderTo() :YielderTo(CORO_NS::noop_coroutine()) { }

        bool await_ready() noexcept { return false; }

        coro_handle await_suspend(coro_handle) noexcept {return _consumer;}

        void await_resume() noexcept {}

    private:
        coro_handle _consumer; // The coroutine that's awaiting my result, or null if none.
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
    std::string CoroutineName(coro_handle);

    /** Writes `CoroutineName(h)` to `out` */
    std::ostream& operator<< (std::ostream& out, coro_handle h);

}
