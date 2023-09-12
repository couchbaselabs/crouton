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
#include "CoroLifecycle.hh"
#include <stdexcept>
#include <optional>
#include <vector>

namespace crouton {
    class Suspension;
    template <class SELF, bool EAGER> class CoroutineImpl;

    /** Base class for the public object returned by a coroutine function.
        Most of the implementation is usually in the associated CoroutineImpl subclass (IMPL). */
    template <class IMPL>
    class Coroutine {
    public:
        // movable, but not copyable.
        Coroutine(Coroutine&& c)                    :_handle(c._handle) {c._handle = {};}
        ~Coroutine()                                {if (_handle) _handle.destroy();}

        using promise_type = IMPL;                  // The name `promise_type` is magic here

        /// Returns my Impl object.
        IMPL& impl()                                {return _handle.promise();}

    protected:
        using handle_type = CORO_NS::coroutine_handle<IMPL>;

        Coroutine() = default;
        explicit Coroutine(handle_type h)           :_handle(h) {}
        handle_type handle()                        {return _handle;}
        void setHandle(handle_type h)               {_handle = h;}
    private:
        friend class CoroutineImpl<IMPL,false>;
        friend class CoroutineImpl<IMPL,true>;
        Coroutine(Coroutine const&) = delete;
        Coroutine& operator=(Coroutine const&) = delete;

        handle_type _handle;    // The internal coroutine handle
    };



    template <bool SUS>
    struct SuspendInitial : public CORO_NS::suspend_always {
        SuspendInitial(coro_handle h) :_handle(h) { };
        constexpr bool await_ready() const noexcept { return !SUS; }
    private:
        coro_handle _handle;
    };

    struct SuspendFinal : public CORO_NS::suspend_always {
    public:
        void await_suspend(coro_handle cur) const noexcept {
            lifecycle::finalSuspend(cur, nullptr);
        }
    };

    struct SuspendFinalTo : public CORO_NS::suspend_always {
    public:
        explicit SuspendFinalTo(coro_handle t) :_target(t) { };
        coro_handle await_suspend(coro_handle cur) const noexcept {
            return lifecycle::finalSuspend(cur, _target);
        }
        coro_handle _target;
    };



    class CoroutineImplBase {
    public:
        CoroutineImplBase() = default;
        ~CoroutineImplBase()                        {lifecycle::ended(_handle);}

        coro_handle handle() const                  {assert(_handle); return _handle;}

        //---- C++ coroutine internal API:

        // Called if an exception is thrown from the coroutine function.
        void unhandled_exception()                      {lifecycle::threw(_handle);}

        // Invoked after the coroutine terminates for any reason.
        // "You must not return a value that causes the terminated coroutine to try to continue
        // running! The only useful thing you might do in this method other than returning straight
        // to the  caller is to transfer control to a different suspended coroutine."
        SuspendFinal final_suspend() noexcept { return {}; }

        /* Other important methods for subclasses:
            INSTANCE get_return_object() {return INSTANCE(handle());}
            T yield_value(U value) { ... }
            void return_value(V value) { ... }
            void return_void() { ... }
         */

    protected:
        CoroutineImplBase(CoroutineImplBase const&) = delete;
        CoroutineImplBase(CoroutineImplBase&&) = delete;

        void registerHandle(coro_handle handle, bool ready, std::type_info const& implType) {
            _handle = handle;
            lifecycle::created(_handle, ready, implType);
        }

        coro_handle _handle;
    };



    /** Base class for a coroutine implementation or "promise_type".
        `SELF` must be the subclass you're defining (i.e. this uses the CRTP.) */
    template <class SELF, bool EAGER =false>
    class CoroutineImpl : public CoroutineImplBase {
    public:
        using handle_type = CORO_NS::coroutine_handle<SELF>;

        handle_type typedHandle()          {
            auto h = handle_type::from_promise((SELF&)*this);
            if (!_handle) registerHandle(h, EAGER, typeid(SELF));
            return h;
        }

        // Determines whether the coroutine starts suspended when created, or runs immediately.
        SuspendInitial<!EAGER> initial_suspend()       {return {handle()};}
    };



    /** General purpose awaiter that manages control flow during `co_yield`.
        It arranges for a specific 'consumer' coroutine, given in the constructor,
        to be resumed by the `co_yield` call. It can be `CORO_NS::noop_coroutine()` to instead resume
        the outer non-coro code that called `resume`. */
    class YielderTo : public CORO_NS::suspend_always {
    public:
        /// Arranges for `consumer` to be returned from `await_suspend`, making it the next
        /// coroutine to run after the `co_yield`.
        explicit YielderTo(coro_handle consumer) : _consumer(consumer) {}

        /// Arranges for the outer non-coro caller to be resumed after the `co_yield`.
        explicit YielderTo() :YielderTo(CORO_NS::noop_coroutine()) { }

        coro_handle await_suspend(coro_handle cur) noexcept {
            return lifecycle::yieldingTo(cur, _consumer);
        }

    private:
        coro_handle _consumer; // The coroutine that's awaiting my result, or null if none.
    };



    /** A cooperative mutex. The first coroutine to `co_await` it will receive a `Lock`
        object, without blocking. From then on, any other coroutine that awaits the mutex will
        block. When the lock goes out of scope it means the first coroutine is done with the
        mutex. The first waiter (if any) will be resumed, getting its own Lock, and so on.
        @warning  Not thread-safe, despite the name! */
    class CoMutex {
    public:
        class Lock {
        public:
            ~Lock()         {if (_mutex) _mutex->unlock();}
            void unlock()   {auto m = _mutex; _mutex = nullptr; m->unlock();}
        private:
            friend class CoMutex;
            explicit Lock(CoMutex* m) :_mutex(m) { }
            Lock(Lock const&) = delete;
            CoMutex* _mutex;
        };

        bool locked() const                     {return _locked;}

        bool await_ready() noexcept             {auto r = !_locked; _locked = true; return r;}
        coro_handle await_suspend(coro_handle h) noexcept;
        Lock await_resume() noexcept            {return Lock(this);}

    private:
        void unlock();

        std::vector<Suspension*> _waiters;
        bool _locked = false;
    };

}
