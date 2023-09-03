//
// Future.hh
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
#include "Coroutine.hh"
#include "Scheduler.hh"
#include <exception>
#include <cassert>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace crouton {
    template <typename T> class Future;
    template <typename T> class FutureImpl;


    // Internal base class of the shared state co-owned by a Future and its FutureProvider.
    class FutureStateBase {
    public:
        bool hasValue() const;
        coro_handle suspend(coro_handle coro);
        void setException(std::exception_ptr x);

        template <class X>
        void setException(X&& x) requires std::derived_from<X, std::exception> {
            setException(std::make_exception_ptr(std::forward<X>(x)));
        }

    protected:
        void _gotValue();
        void _checkValue();

        mutable std::mutex  _mutex;                 // For thread-safety
        Suspension*         _suspension = nullptr;  // coro blocked awaiting my Future
        std::exception_ptr  _exception;             // The exception, if any
        bool                _hasValue = false;      // True when a value or exception is set
    };

    // Internal state shared by a Future and its FutureProvider
    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        T&& value() {
            std::unique_lock<std::mutex> lock(_mutex);
            _checkValue();
            assert(_value);
            return std::move(*_value);
        }

        void setValue(T&& value) {
            std::unique_lock<std::mutex> lock(_mutex);
            _value.emplace(std::move(value));
            _gotValue();
        }

        void setValue(T const& value) {
            std::unique_lock<std::mutex> lock(_mutex);
            _value.emplace(value);
            _gotValue();
        }

    private:
        std::optional<T> _value {};
    };

    template <>
    class FutureState<void> : public FutureStateBase {
    public:
        void value();
        void setValue();
    };



    /** The producer side of a Future, which creates the Future object and is responsible for
        setting its value. Use this if you want to create a Future without being a coroutine. */
    template <typename T>
    class FutureProvider {
    public:
        /// Constructs a Future that doesn't have a value yet.
        FutureProvider()                        {reset();}

        /// Creates a Future that can be returned to callers.
        Future<T> future()                      {return Future<T>(*this);}

        /// True if there is a value.
        bool hasValue() const                   {return _state->hasValue();}

        /// Sets the Future's value and unblocks anyone waiting for it.
        void setValue(T&& t) const              {_state->setValue(std::move(t));}
        void setValue(T const& t) const         {_state->setValue(t);}

        /// Sets the Future's result as an exception and unblocks anyone waiting for it.
        /// Calling value() will re-throw the exception.
        template <class X>
            void setException(X&& x) const     {_state->setException(std::forward<X>(x));}

        /// Gets the future's value, or throws its exception.
        /// It's illegal to call this before a value is set.
        T&& value() const                       {return std::move(_state->value());}

        /// Clears the provider, detaching it from its current Future, so it can create another.
        void reset()                            {_state = std::make_shared<FutureState<T>>();}
    private:
        friend class Future<T>;
        std::shared_ptr<FutureState<T>> _state;
    };

    template <>
    class FutureProvider<void> {
    public:
        FutureProvider()                        {reset();}
        Future<void> future();
        bool hasValue() const                   {return _state->hasValue();}
        void setValue() const                   {_state->setValue();}
        template <class X>
            void setException(X&& x) const      {_state->setException(std::forward<X>(x));}
        void value() const                      {return _state->value();}
        void reset()                            {_state = std::make_shared<FutureState<void>>();}
    private:
        friend class Future<void>;
        std::shared_ptr<FutureState<void>> _state;
    };



    /** Represents a value, produced by a `FutureProvider<T>`, that may not be available yet.
        This is a typical type for a coroutine to return.
        A coroutine can get the value by calling `co_await` on it, which suspends the coroutine
        until the value is available. */
    template <typename T>
    class Future : public Coroutine<FutureImpl<T>> {
    public:
        Future(FutureProvider<T> &provider)   :_state(provider._state) { }

        /// Creates an already-ready Future
        Future(T&& value)
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setValue(std::move(value));
        }

        /// Creates an already-failed future :(
        template <class X>
        Future(X&& x) requires std::derived_from<X, std::exception>
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setException(std::forward<X>(x));
        }

        /// True if a value or exception has been set by the provider.
        bool hasValue() const           {return _state->hasValue();}

        /// Returns the value, or throws the exception. Don't call this if hasValue is false.
        T&& value() const               {return _state->value();}

        /// Blocks until the value is available. Must NOT be called from a coroutine!
        /// Requires that this Future be returned from a coroutine.
        [[nodiscard]] T&& waitForValue() {return std::move(this->handle().promise().waitForValue());}

        // These methods make Future awaitable:
        bool await_ready()              {return _state->hasValue();}
        [[nodiscard]] T&& await_resume(){return std::move(_state->value());}
        auto await_suspend(coro_handle coro) noexcept {return _state->suspend(coro);}

    private:
        friend class FutureProvider<T>;
        friend class FutureImpl<T>;

        std::shared_ptr<FutureState<T>>  _state;
    };

    template <>
    class Future<void> : public Coroutine<FutureImpl<void>> {
    public:
        /// Creates an already-ready Future.
        Future()                :_state(std::make_shared<FutureState<void>>()) {_state->setValue();}
        Future(FutureProvider<void> &p) :_state(p._state) { }

		/// Creates an already-failed future :(
        template <class X>
        Future(X&& x) requires std::derived_from<X, std::exception>
        :_state(std::make_shared<FutureState<void>>()) {
            _state->setException(std::forward<X>(x));
        }
        bool hasValue() const           {return _state->hasValue();}
        void value() const              {_state->value();}
        inline void waitForValue();
        // These methods make Future awaitable:
        bool await_ready()              {return _state->hasValue();}
        void await_resume()             {_state->value();}
        auto await_suspend(coro_handle coro) noexcept {return _state->suspend(coro);}

    protected:
        friend class FutureProvider<void>;
        friend class FutureImpl<void>;

        Future(std::shared_ptr<FutureState<void>> state)   :_state(std::move(state)) { }

        std::shared_ptr<FutureState<void>>  _state;
    };


#pragma mark - FUTURE IMPL:


    // Implementation (promise_type) of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<FutureImpl<T>> {
    public:
        using super = CoroutineImpl<FutureImpl<T>>;
        using handle_type = typename super::handle_type;

        FutureImpl() = default;

        T&& waitForValue() {
            Scheduler::current().runUntil([&]{ return _provider.hasValue(); });
            return std::move(_provider.value());
        }

        //---- C++ coroutine internal API:

        Future<T> get_return_object() {
            auto f = _provider.future();
            f.setHandle(this->handle());
            return f;
        }

        CORO_NS::suspend_never initial_suspend() {
            //std::cerr << "New " << typeid(this).name() << " " << handle() << std::endl;
            return {};
        }
        void unhandled_exception()              {_provider.setException(std::current_exception());}
        template <class X>  // you can co_return an exception
        void return_value(X&& x) requires std::derived_from<X, std::exception> {
            _provider.setException(std::forward<X>(x));
        }
        void return_value(T&& value)            {_provider.setValue(std::move(value));}
        void return_value(T const& value)       {_provider.setValue(value);}
        Finisher final_suspend() noexcept       {return {};}

    protected:
        FutureProvider<T> _provider;
    };


    template <>
    class FutureImpl<void> : public CoroutineImpl<FutureImpl<void>> {
    public:
        using super = CoroutineImpl<FutureImpl<void>>;
        using handle_type = super::handle_type;
        FutureImpl() = default;
        void waitForValue();
        Future<void> get_return_object();
        CORO_NS::suspend_never initial_suspend()    {
            //std::cerr << "New " << typeid(this).name() << " " << handle() << std::endl;
            return {};
        }
        void unhandled_exception()              {_provider.setException(std::current_exception());}
        void return_void()                      {_provider.setValue();}
        Finisher final_suspend() noexcept       {return {};}
    private:
        handle_type handle()                    {return handle_type::from_promise(*this);}
        FutureProvider<void> _provider;
    };

    void Future<void>::waitForValue()             {this->handle().promise().waitForValue();}

}
