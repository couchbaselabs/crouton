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
#include "Result.hh"
#include "Scheduler.hh"
#include <atomic>
#include <exception>
#include <memory>
#include <utility>
#include <cassert>

/// Macro for declaring a function that returns a Future, e.g. `ASYNC<void> close();`
/// It's surprisingly easy to forget to await the Future, especially `Future<void>`,
/// hence the `[[nodiscard]]` annotation.
#define ASYNC [[nodiscard]] Future


namespace crouton {
    template <typename T> class FutureImpl;
    template <typename T> class FutureState;

    template <typename T> using FutureProvider = std::shared_ptr<FutureState<T>>;

    /** Represents a value of type `T` that may not be available yet.

        This is a typical type for a coroutine to return. The coroutine function can just
        `co_return` a value of type `T`, or return a `std::exception` subclass to indicate failure,
        or even throw an exception.

        A coroutine that gets a Future from a function call should `co_await` it to get its value.
        Or if the Future resolves to an exception, the `co_await` will re-throw it.

        A regular function can return a Future by creating a `FutureProvider` local variable and
        returning it, which implicitly creates a `Future` from it. It needs to arrange, via a
        callback or another thread, to call `setResult` or `setException` on the provider; this
        resolves the future and unblocks anyone waiting. If the function finds it can return a
        value immediately, it can just return a Future constructed with its value (or exception.)

        A regular function that gets a Future can call `then()` to register a callback. */
    template <typename T = void>
    class Future : public Coroutine<FutureImpl<T>> {
    public:
        /// Creates a Future from a FutureProvider.
        explicit Future(FutureProvider<T> state)   :_state(std::move(state)) {assert(_state);}

        /// Creates an already-ready Future.
        /// @note In `Future<void>`, this constructor takes no parameters.
        Future(T&& value)
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setResult(std::move(value));
        }

        /// Creates an already-failed future :(
        template <class X>
        Future(X&& x) requires std::derived_from<X, std::exception>
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setResult(std::forward<X>(x));
        }

        Future(Future&&) = default;
        ~Future()                       {this->setHandle(nullptr);} // don't destroy handle

        /// True if a value or exception has been set by the provider.
        bool hasResult() const           {return _state->hasResult();}

        /// Returns the result, or throws the exception. Don't call this if hasResult is false.
        T&& result() const               {return _state->result();}

        /// Registers a callback that will be called when the result is available, and which can
        /// return a new value (or void) which becomes the result of the returned Future.
        /// @param fn A callback that will be called when the value is available.
        /// @returns  A new Future whose result will be the return value of the callback,
        ///           or a `Future<void>` if the callback doesn't return a value.
        /// @note  If this Future already has a result, the callback is called immediately,
        ///        before `then` returns, and thus the returned Future will also have a result.
        /// @note  If this Future fails with an exception, the callback will not be called.
        ///        Instead the returned Future's result will be the same exception.
        template <typename FN, typename U = std::invoke_result_t<FN,T>>
        Future<U> then(FN fn);

        //---- These methods make Future awaitable:
        bool await_ready() {
            return _state->hasResult();
        }
        auto await_suspend(coro_handle coro) noexcept {
            auto next = _state->suspend(coro);
            return lifecycle::suspendingTo(coro, this->handle(), next);
        }
        [[nodiscard]] T&& await_resume(){
            return std::move(_state->result());
        }

    private:
        using super = Coroutine<FutureImpl<T>>;
        friend class FutureImpl<T>;

        Future(typename super::handle_type h, FutureProvider<T> state)
        :super(h)
        ,_state(std::move(state))
        {assert(_state);}

        FutureProvider<T>  _state;
    };


#pragma mark - FUTURE STATE:


    // Internal base class of FutureState<T>.
    class FutureStateBase {
    public:
        bool hasResult() const                       {return _state.load() == Ready;}

        coro_handle suspend(coro_handle coro);

        virtual void setException(std::exception_ptr) = 0;
        virtual std::exception_ptr getException() = 0;

        using ChainCallback = std::function<void(FutureStateBase&,FutureStateBase&)>;

        template <typename U>
        Future<U> chain(ChainCallback fn) {
            auto provider = std::make_shared<FutureState<U>>();
            _chain(provider, fn);
            return Future<U>(std::move(provider));
        }

    protected:
        enum State : uint8_t {
            Empty,      // initial state
            Waiting,    // a coroutine is waiting and _suspension is set
            Chained,    // another Future is chained to this one with `then(...)`
            Ready       // result is available and _result is set
        };

        virtual ~FutureStateBase() = default;
        bool checkEmpty();
        bool changeState(State);
        void _notify();
        void _chain(std::shared_ptr<FutureStateBase>, ChainCallback);
        void resolveChain();

        Suspension*                      _suspension = nullptr;  // coro that's awaiting result
        std::shared_ptr<FutureStateBase> _chainedFuture;         // Future of a 'then' callback
        ChainCallback                    _chainedCallback;       // 'then' callback
        std::atomic<State>               _state = Empty;         // Current state, for thread-safety
    };


    /** The actual state of a Future. It's used to set the result/exception. */
    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        T&& result() {
            assert(hasResult());
            return std::move(_result).value();
        }

        template <typename U>
        void setResult(U&& value) {
            _result = std::forward<U>(value);
            _notify();
        }

        void setException(std::exception_ptr x) override {setResult(x);}
        std::exception_ptr getException() override       {return _result.exception();}

    private:
        Result<T> _result;
    };


    template <>
    class FutureState<void> : public FutureStateBase {
    public:
        void result() {
            assert(hasResult());
            return _result.value();
        }
        void setResult() {
            _result.set();
            _notify();
        }
        template <typename U>
        void setResult(U&& value) {
            _result = std::forward<U>(value);
            _notify();
        }

        void setException(std::exception_ptr x) override {setResult(x);}
        std::exception_ptr getException() override       {return _result.exception();}

    private:
        Result<void> _result;
    };


    template <>
    class Future<void> : public Coroutine<FutureImpl<void>> {
    public:
        Future()                :_state(std::make_shared<FutureState<void>>()) {_state->setResult();}
        explicit Future(FutureProvider<void> p) :_state(std::move(p)) {assert(_state);}
        template <class X>
        Future(X&& x) requires std::derived_from<X, std::exception>
        :_state(std::make_shared<FutureState<void>>()) {_state->setResult(std::forward<X>(x));}
        Future(Future&&) = default;
        ~Future()                        {this->setHandle(nullptr);} // don't destroy handle
        bool hasResult() const           {return _state->hasResult();}
        void result() const              {_state->result();}
        template <typename FN, typename U = std::invoke_result_t<FN>>
        Future<U> then(FN);
        bool await_ready()              {return _state->hasResult();}
        auto await_suspend(coro_handle coro) noexcept {
            return lifecycle::suspendingTo(coro, this->handle(), _state->suspend(coro));
        }
        void await_resume()             {_state->result();} // just check for an exception
    protected:
        using super = Coroutine<FutureImpl<void>>;
        friend class FutureImpl<void>;

        Future(typename super::handle_type h, FutureProvider<void> state)
        :super(h)
        ,_state(std::move(state))
        {assert(_state);}

        FutureProvider<void>  _state;
    };


    template <typename T>
    template <typename FN, typename U>
    Future<U> Future<T>::then(FN fn) {
        return _state->template chain<U>([fn](FutureStateBase& baseState, FutureStateBase& myBaseState) {
            auto& state = dynamic_cast<FutureState<U>&>(baseState);
            T&& result = dynamic_cast<FutureState<T>&>(myBaseState).result();
            if constexpr (std::is_void_v<U>) {
                fn(std::move(result));
                state.setResult();
            } else {
                state.setResult(fn(std::move(result)));
            }
        });
    }

    template <typename FN, typename U>
    Future<U> Future<void>::then(FN fn) {
        return _state->template chain<U>([fn](FutureStateBase& baseState, FutureStateBase&) {
            auto& state = dynamic_cast<FutureState<U>&>(baseState);
            if constexpr (std::is_void_v<U>) {
                fn();
                state.setResult();
            } else {
                state.setResult(fn());
            }
        });
    }


#pragma mark - FUTURE IMPL:


    // Implementation (promise_type) of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<FutureImpl<T>, true> {
    public:
        using super = CoroutineImpl<FutureImpl<T>, true>;
        using handle_type = typename super::handle_type;

        FutureImpl() = default;

        //---- C++ coroutine internal API:

        Future<T> get_return_object() {
            return Future(this->typedHandle(), _provider);
        }

        void unhandled_exception() {
            this->super::unhandled_exception();
            _provider->setResult(std::current_exception());
        }

        template <class X>  // you can co_return an exception
        void return_value(X&& x) requires std::derived_from<X, std::exception> {
            lifecycle::returning(this->handle());
            _provider->setResult(std::forward<X>(x));
        }
        void return_value(T&& value) {
            lifecycle::returning(this->handle());
            _provider->setResult(std::move(value));
        }
        void return_value(T const& value) {
            lifecycle::returning(this->handle());
            _provider->setResult(value);
        }

        auto final_suspend() noexcept {
            struct finalizer : public CORO_NS::suspend_always {
                void await_suspend(coro_handle cur) noexcept {
                    lifecycle::finalSuspend(cur, nullptr);
                    cur.destroy();
                }
            };
            return finalizer{};
        }

    protected:
        FutureProvider<T> _provider = std::make_shared<FutureState<T>>();
    };


    template <>
    class FutureImpl<void> : public CoroutineImpl<FutureImpl<void>, true> {
    public:
        using super = CoroutineImpl<FutureImpl<void>, true>;
        using handle_type = super::handle_type;
        FutureImpl() = default;
        Future<void> get_return_object() {return Future(typedHandle(), _provider);}
        void unhandled_exception() {
            this->super::unhandled_exception();
            _provider->setResult(std::current_exception());
        }
        void return_void() {
            lifecycle::returning(handle());
            _provider->setResult();
        }
        auto final_suspend() noexcept {
            struct finalizer : public CORO_NS::suspend_always {
                void await_suspend(coro_handle cur) noexcept {
                    lifecycle::finalSuspend(cur, nullptr);
                    cur.destroy();
                }
            };
            return finalizer{};
        }

    private:
        FutureProvider<void> _provider = std::make_shared<FutureState<void>>();
    };

}
