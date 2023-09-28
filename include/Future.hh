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
#include "Select.hh"

#include <atomic>
#include <exception>
#include <functional>

/// Macro for declaring a function that returns a Future, e.g. `ASYNC<void> close();`
/// It's surprisingly easy to forget to await the Future, especially `Future<void>`,
/// hence the `[[nodiscard]]` annotation.
#define        ASYNC [[nodiscard]]         crouton::Future
#define  staticASYNC [[nodiscard]] static  crouton::Future
#define virtualASYNC [[nodiscard]] virtual crouton::Future


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
    template <typename T>
    class Future : public Coroutine<FutureImpl<T>>, public ISelectable {
    public:
        using nonvoidT = std::conditional<std::is_void_v<T>, std::byte, T>::type;

        /// Creates a Future from a FutureProvider.
        explicit Future(FutureProvider<T> state)        :_state(std::move(state)) {assert(_state);}

        /// Creates an already-ready `Future`.
        /// @note In `Future<void>`, this constructor takes no parameters.
        Future(nonvoidT&& v)  requires (!std::is_void_v<T>) {_state->setResult(std::move(v));}

        /// Creates an already-ready `Future<void>`.
        Future()  requires (std::is_void_v<T>)          {_state->setResult();}

        /// Creates an already-failed future :(
        Future(Error err)                               {_state->setResult(err);}
        Future(ErrorDomain auto d)                      :Future(Error(d)) { }

        Future(Future&&) = default;
        ~Future()                                       {if (_state) _state->noFuture();}

        /// True if a value or error has been set by the provider.
        bool hasResult() const                          {return _state->hasResult();}

        /// Returns the result, or throws the exception. Don't call this if hasResult is false.
        std::add_rvalue_reference_t<T> result() const   {return _state->resultValue();}

        /// Registers a callback that will be called when the result is available, and which can
        /// return a new value (or void) which becomes the result of the returned Future.
        /// @param fn A callback that will be called when the value is available.
        /// @returns  A new Future whose result will be the return value of the callback,
        ///           or a `Future<void>` if the callback doesn't return a value.
        /// @note  If this Future already has a result, the callback is called immediately,
        ///        before `then` returns, and thus the returned Future will also have a result.
        /// @note  If this Future fails with an exception, the callback will not be called.
        ///        Instead the returned Future's result will be the same exception.
        template <typename FN, typename U = std::invoke_result_t<FN,T>> requires(!std::is_void_v<T>)
        [[nodiscard]] Future<U> then(FN fn);

        template <typename FN, typename U = std::invoke_result_t<FN>> requires(std::is_void_v<T>)
        [[nodiscard]] Future<U> then(FN);

        /// From ISelectable interface.
        virtual void onReady(OnReadyFn fn)  {_state->onReady(std::move(fn));}

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
        [[nodiscard]] std::add_rvalue_reference_t<T> await_resume() requires (!std::is_void_v<T>) {
            return std::move(_state->resultValue());
        }

        void await_resume() requires (std::is_void_v<T>) {
            _state->resultValue();
        }

    private:
        using super = Coroutine<FutureImpl<T>>;
        friend class FutureImpl<T>;
        friend class NoThrow<T>;

        Future(typename super::handle_type h, FutureProvider<T> state)
        :super(h)
        ,_state(std::move(state))
        {assert(_state);}

        FutureProvider<T> _state = std::make_shared<FutureState<T>>();
    };


#pragma mark - FUTURE STATE:


    // Internal base class of FutureState<T>.
    class FutureStateBase {
    public:
        bool hasResult() const                       {return _state.load() == Ready;}

        void onReady(ISelectable::OnReadyFn);   // Called by Future::onReady
        void noFuture();                        // Called by Future::~Future
        coro_handle suspend(coro_handle coro);  // Called by Future::await_suspend

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

        Suspension                       _suspension;           // coro that's awaiting result
        std::shared_ptr<FutureStateBase> _chainedFuture;        // Future of a 'then' callback
        ChainCallback                    _chainedCallback;      // 'then' callback
        ISelectable::OnReadyFn           _onReady;              // `onReady` callback
        std::atomic<bool>                _hasOnReady = false;
        std::atomic<State>               _state = Empty;        // Current state, for thread-safety
    };


    /** The actual state of a Future. It's used to set the result/error. */
    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        Result<T> && result() &&                        {return std::move(_result);}
        Result<T> & result() &                          {return _result;}

        std::add_rvalue_reference_t<T> resultValue()  requires (!std::is_void_v<T>) {
            assert(hasResult());
            return std::move(_result).value();
        }

        void resultValue()  requires (std::is_void_v<T>) {
            assert(hasResult());
            _result.value();
        }

        template <typename U>
        void setResult(U&& value)  requires (!std::is_void_v<T>) {
            _result = std::forward<U>(value);
            _notify();
        }

        void setResult()  requires (std::is_void_v<T>) {
            _result.set();
            _notify();
        }
        void setResult(Error err)  requires (std::is_void_v<T>) {
            if (err)
                _result = err;
            else
                _result.set(); // set success
            _notify();
        }

        void setError(Error x) override                 {setResult(x);}
        Error getError() override                       {return _result.error();}

    private:
        Result<T> _result;
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


#pragma mark - FUTURE IMPL:


    // Implementation (promise_type) of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<FutureImpl<T>, true> {
    public:
        using super = CoroutineImpl<FutureImpl<T>, true>;
        using handle_type = typename super::handle_type;
        using nonvoidT = std::conditional<std::is_void_v<T>, std::byte, T>::type;

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

        void return_value(nonvoidT&& value)  requires (!std::is_void_v<T>) {
            lifecycle::returning(this->handle());
            _provider->setResult(std::move(value));
        }
        void return_value(nonvoidT const& value)  requires (!std::is_void_v<T>) {
            lifecycle::returning(this->handle());
            _provider->setResult(value);
        }

    protected:
        FutureProvider<T> _provider = std::make_shared<FutureState<T>>();
    };



    // Future<T>::then, for T != void
    template <typename T>
    template <typename FN, typename U>  requires(!std::is_void_v<T>)
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

    // Future<T>::then, for T == void
    template <typename T>
    template <typename FN, typename U>  requires(std::is_void_v<T>)
    Future<U> Future<T>::then(FN fn) {
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

}
