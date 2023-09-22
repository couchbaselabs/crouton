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
#include <functional>
#include <memory>
#include <utility>
#include <cassert>

/// Macro for declaring a function that returns a Future, e.g. `ASYNC<void> close();`
/// It's surprisingly easy to forget to await the Future, especially `Future<void>`,
/// hence the `[[nodiscard]]` annotation.
#define        ASYNC [[nodiscard]] Future
#define  staticASYNC [[nodiscard]] static Future
#define virtualASYNC [[nodiscard]] virtual Future


namespace crouton {
    template <typename T> class FutureImpl;
    template <typename T> class FutureState;
    template <typename T> class NoThrow;

    template <typename T> using FutureProvider = std::shared_ptr<FutureState<T>>;

    /** Represents a value of type `T` that may not be available yet.

        This is a typical type for a coroutine to return. The coroutine function can just
        `co_return` a value of type `T`, or an `Error` to indicate failure,
        or even throw an exception.

        A coroutine that gets a Future from a function call should `co_await` it to get its value.
        Or if the Future resolves to an exception, the `co_await` will re-throw it.

        A regular function can return a Future by creating a `FutureProvider` local variable and
        returning it, which implicitly creates a `Future` from it. It needs to arrange, via a
        callback or another thread, to call `setResult` or `setError` on the provider; this
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
        Future(Error err)
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setResult(err);
        }

        Future(Future&&) = default;
        ~Future()                       {this->setHandle(nullptr);} // don't destroy handle

        /// True if a value or error has been set by the provider.
        bool hasResult() const           {return _state->hasResult();}

        /// Returns the result, or throws the exception. Don't call this if hasResult is false.
        T&& result() const               {return _state->resultValue();}

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
        [[nodiscard]] Future<U> then(FN fn);

        //---- These methods make Future awaitable:
        bool await_ready() {
            return _state->hasResult();
        }
        auto await_suspend(coro_handle coro) noexcept {
            if (this->handle())
                return lifecycle::suspendingTo(coro, this->handle(), _state->suspend(coro));
            else
                return lifecycle::suspendingTo(coro, typeid(this), this, _state->suspend(coro));
        }
        [[nodiscard]] T&& await_resume(){
            return std::move(_state->resultValue());
        }

    private:
        using super = Coroutine<FutureImpl<T>>;
        friend class FutureImpl<T>;
        friend class NoThrow<T>;

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

        virtual void setError(Error) = 0;
        virtual Error getError() = 0;

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


    /** The actual state of a Future. It's used to set the result/error. */
    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        Result<T> && result() &&                        {return std::move(_result);}
        Result<T> & result() &                          {return _result;}

        T&& resultValue() {
            assert(hasResult());
            return std::move(_result).value();
        }

        template <typename U>
        void setResult(U&& value) {
            _result = std::forward<U>(value);
            _notify();
        }

        void setError(Error x) override                 {setResult(x);}
        Error getError() override                       {return _result.error();}

    private:
        Result<T> _result{Error{}};
    };


    template <>
    class FutureState<void> : public FutureStateBase {
    public:
        Result<void> && result() &&                     {return std::move(_result);}
        Result<void> & result() &                       {return _result;}

        void resultValue() {
            assert(hasResult());
            return _result.value();
        }
        void setResult() {
            _notify();
        }
        void setResult(Error err) {
            if (err) _result = err;
            _notify();
        }

        void setError(Error x) override                 {setResult(x);}
        Error getError() override                       {return _result.error();}

    private:
        Result<void> _result;
    };


    template <>
    class Future<void> : public Coroutine<FutureImpl<void>> {
    public:
        Future()                :_state(std::make_shared<FutureState<void>>()) {_state->setResult();}
        explicit Future(FutureProvider<void> p) :_state(std::move(p)) {assert(_state);}
        Future(Error err) :_state(std::make_shared<FutureState<void>>()) {_state->setResult(err);}
        Future(Future&&) = default;
        ~Future()                        {this->setHandle(nullptr);} // don't destroy handle
        bool hasResult() const           {return _state->hasResult();}
        void result() const              {_state->resultValue();}
        template <typename FN, typename U = std::invoke_result_t<FN>>
        Future<U> then(FN);
        bool await_ready()              {return _state->hasResult();}
        auto await_suspend(coro_handle coro) noexcept {
            return lifecycle::suspendingTo(coro, this->handle(), _state->suspend(coro));
        }
        void await_resume()             {_state->resultValue();} // just check for an exception
    protected:
        using super = Coroutine<FutureImpl<void>>;
        friend class FutureImpl<void>;
        friend class NoThrow<void>;

        Future(typename super::handle_type h, FutureProvider<void> state)
        :super(h)
        ,_state(std::move(state))
        {assert(_state);}

        FutureProvider<void>  _state;
    };


    /** Wrap this around a Future before co_await'ing it, to get the value as a Result.
        This will not throw; instead, you have to check the Result for an error. */
    template <typename T>
    class NoThrow {
    public:
        NoThrow(Future<T>&& future)     :_handle(future.handle()), _state(std::move(future._state)) { }

        bool hasResult() const          {return _state->hasResult();}
        Result<T> const& result() &     {return _state->result();}
        Result<T> result() &&           {return std::move(_state)->result();}

        bool await_ready() noexcept     {return _state->hasResult();}
        auto await_suspend(coro_handle coro) noexcept {
            return lifecycle::suspendingTo(coro, _handle, _state->suspend(coro));
        }
        [[nodiscard]] Result<T> await_resume() noexcept {return std::move(_state)->result();}

    private:
        coro_handle        _handle;
        FutureProvider<T>  _state;
    };


    template <typename T>
    template <typename FN, typename U>
    Future<U> Future<T>::then(FN fn) {
        return _state->template chain<U>([fn](FutureStateBase& baseState, FutureStateBase& myBaseState) {
            auto& state = dynamic_cast<FutureState<U>&>(baseState);
            T&& result = dynamic_cast<FutureState<T>&>(myBaseState).resultValue();
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
            return Future<T>(this->typedHandle(), _provider);
        }

        void unhandled_exception() {
            this->super::unhandled_exception();
            _provider->setResult(Error(std::current_exception()));
        }

        void return_value(Error err) {
            lifecycle::returning(this->handle());
            _provider->setResult(err);
        }
        void return_value(ErrorDomain auto errVal) {
            return_value(Error(errVal));
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
        Future<void> get_return_object() {return Future<void>(typedHandle(), _provider);}
        void unhandled_exception() {
            this->super::unhandled_exception();
            _provider->setResult(Error(std::current_exception()));
        }
#if 1
        void return_value(Error err) {
            lifecycle::returning(this->handle());
            _provider->setResult(err);
        }
        void return_value(ErrorDomain auto errVal) {
            return_value(Error(errVal));
        }
#else
        void return_void() {
            lifecycle::returning(handle());
            _provider->setResult();
        }
#endif
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
