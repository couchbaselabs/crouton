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
#include "Base.hh"
#include <stdexcept>
#include <optional>
#include <vector>

namespace crouton {
    template <class SELF> class CoroutineImpl;

    using coro_handle = CORO_NS::coroutine_handle<>;


    /** Base class for the public object returned by a coroutine function.
        Most of the implementation is usually in the associated CoroutineImpl subclass (IMPL). */
    template <class IMPL>
    class Coroutine {
    public:
        // movable, but not copyable.
        Coroutine(Coroutine&& c)        :_handle(c._handle) {c._handle = {};}
        ~Coroutine()                          {if (_handle) _handle.destroy();}

        using promise_type = IMPL;                  // The name `promise_type` is magic here

        /// Returns my Impl object.
        IMPL& impl()                                {return _handle.promise();}

    protected:
        using handle_type = CORO_NS::coroutine_handle<IMPL>;

        Coroutine() = default;
        explicit Coroutine(handle_type h)     :_handle(h) {}
        handle_type handle()                        {return _handle;}
        void setHandle(handle_type h)               {_handle = h;}
    private:
        friend class CoroutineImpl<IMPL>;
        Coroutine(Coroutine const&) = delete;
        Coroutine& operator=(Coroutine const&) = delete;

        handle_type _handle;    // The internal coroutine handle
    };



    /** Base class for a coroutine implementation or "promise_type".
        `SELF` must be the subclass you're defining (i.e. this uses the CRTP.) */
    template <class SELF>
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
        CoroutineImpl(CoroutineImpl const&) = delete;
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



    /** A cooperative mutex. The first coroutine to `co_await` it gets back a `Lock`
        object, without blocking. From then on, any other coroutine that awaits the mutex will
        block. When the lock goes out of scope it means the first coroutine is done with the
        mutex. The first waiter (if any) will be resumed, getting its own Lock, and so on. */
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

        bool locked() const {return _busy;}

        bool await_ready() noexcept {
            auto ready = !_busy; _busy = true; return ready;
        }

        coro_handle await_suspend(coro_handle h) noexcept {
            _waiters.push_back(h);
            return std::noop_coroutine();
        }

        Lock await_resume() noexcept {
            return Lock(this);
        }

    private:
        void unlock() {
            if (_waiters.empty())
                _busy = false;
            else {
                coro_handle next = _waiters.front();
                _waiters.erase(_waiters.begin());
                next.resume();
            }
        }

        bool _busy = false;
        std::vector<coro_handle> _waiters;
    };



    /** A cooperative condition variable. A coroutine that `co_await`s it will block until
        something calls `notify`, passing in a value. That wakes up the waiting coroutine and
        returns that value as the result of `co_await`. The CoCondition is then back in its
        empty state and can be reused, if desired.

        If `notify` is called first, the `co_await` doesn't block, it just returns the value.

        This is very useful as an adapter for callback-based asynchronous code like libuv.
        Just create a `CoCondition` and call the asynchronous function with a callback that
        will call `notify` on it. Then `co_await` the `CoCondition`. If the callback is given a
        result value, pass it to `notify` and you'll get it as the result of `co_await`.

        @note It currently doesn't support more than one waiting coroutine, but it wouldn't be hard
        to add that capability (`_waiter` just needs to become a vector/queue.) */
    template <typename T>
    class CoCondition {
    public:
        bool await_ready() noexcept {return _value.has_value();}

        coro_handle await_suspend(coro_handle h) noexcept {
            assert(!_waiter);   // currently only supports a single waiter
            _waiter = h;
            return std::noop_coroutine();
        }

        T&& await_resume() noexcept {
            return std::move(_value).value();
        }

        template <typename U>
        void notify(U&& val) {
            _value.emplace(std::forward<U>(val));
            if (auto w = _waiter) {
                _waiter = nullptr;
                w.resume();
            }
        }

    protected:
        T const& value() const  {return _value.value();}
        
    private:
        coro_handle _waiter;
        std::optional<T> _value;
    };

    template <>
    class CoCondition<void> {
    public:
        bool await_ready() noexcept {return _notified;}

        coro_handle await_suspend(coro_handle h) noexcept {
            assert(!_waiter);   // currently only supports a single waiter
            _waiter = h;
            return std::noop_coroutine();
        }

        void await_resume() noexcept {
            _notified = false;
        }

        void notify() {
            assert(!_notified);
            _notified = true;
            if (auto w = _waiter) {
                _waiter = nullptr;
                w.resume();
            }
        }

    private:
        coro_handle _waiter;
        bool _notified = false;
    };


    // DEPRECATED -- use CoMutex instead.
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
