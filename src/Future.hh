//
// Future.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Coroutine.hh"
#include "Scheduler.hh"
#include <cassert>
#include <exception>

namespace snej::coro {
    class FutureBase;
    class WaiterBase;
    template <typename T> class Future;
    template <typename T> class FutureImpl;
    template <typename T> class FutureState;
    template <typename T> class Waiter;


    /// The producer side of a Future, which is responsible for setting its value.
    template <typename T>
    class FutureProvider {
    public:
        /// Constructs a Future that doesn't have a value yet.
        FutureProvider()                        :_state(std::make_shared<FutureState<T>>()) { }

        /// Constructs a Future that already has a value.
        explicit FutureProvider(T&& t)
        :FutureProvider()
        {
            _state->_value = std::forward<T>(t);
        }

        /// Creates a Future that can be returned to callers.
        Future<T> future()                      {return Future<T>(_state);}
        operator Future<T>()                    {return future();}

        /// True if there is a value.
        bool hasValue() const                   {return _state->hasValue();}

        /// Sets the Future's value and unblocks anyone waiting for it.
        void setValue(T&& t) const              {_state->setValue(std::forward<T>(t));}

        /// Sets the Future's result as an exception and unblocks anyone waiting for it.
        /// Calling value() will re-throw the exception.
        void setException(std::exception_ptr x) {_state->setException(x);}

        /// Gets the future's value, or throws its exception.
        /// It's illegal to call this before a value is set.
        T& value() const                        {return _state->value();}

    private:
        std::shared_ptr<FutureState<T>> _state;
    };



    /// Represents a value, produced by a `FutureProvider<T>`, that may not be available yet.
    /// A coroutine can get the value by calling `co_await` on it, which suspends the coroutine
    /// until the value is available.
    template <typename T>
    class Future : public CoroutineHandle<FutureImpl<T>> {
    public:
        using super = CoroutineHandle<FutureImpl<T>>;

        bool hasValue() const            {return _state->hasValue();}

        /// Blocks until the value is available. Must NOT be called from a coroutine!
        /// Requires that this Future be returned from a coroutine.
        T& waitForValue()               {return super::handle().promise().waitForValue();}

    private:
        friend class Waiter<T>;
        friend class FutureProvider<T>;
        friend class FutureImpl<T>;

        Future(std::shared_ptr<FutureState<T>> state)
        :_state(std::move(state))
        { }

        std::shared_ptr<FutureState<T>>  _state;
    };


#pragma mark - AWAITING A FUTURE:


    class FutureStateBase {
    public:
        bool hasValue() const {
            std::unique_lock<std::mutex> lock(_mutex);
            return _hasValue;
        }

        bool suspend(std::coroutine_handle<> coro) {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_hasValue)
                return false;
            assert(!_suspension);
            Scheduler& sched = Scheduler::current();
            _suspension = sched.suspend(coro);
            return true;
        }

    protected:
        void _gotValue() {
            _hasValue = true;
            if (_suspension) {
                _suspension->wakeUp();
                _suspension = nullptr;
            }
        }

        mutable std::mutex  _mutex;
        Suspension* _suspension = nullptr;
        bool _hasValue = false;
    };


    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        T& value() {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_exception)
                std::rethrow_exception(_exception);
            if (_result)
                return *_result;
            else
                throw std::logic_error("Future does not have a value yet");
        }

        void setValue(T&& value) {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_hasValue)
                throw std::logic_error("Future's value can only be set once");
            _result = std::forward<T>(value);
            _gotValue();
        }

        void setException(std::exception_ptr x) {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_hasValue)
                throw std::logic_error("Future's value can only be set once");
            _exception = x;
            _gotValue();
        }

    private:
        std::optional<T>   _result {};
        std::exception_ptr _exception;
    };



    // Base class of Waiter<T>
    class WaiterBase {
    public:
        WaiterBase(std::shared_ptr<FutureStateBase> state)
        :_state(std::move(state))
        { }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coro) noexcept {
            if (_state->suspend(coro))
                return Scheduler::current().next();
            else
                return coro;
        }

    protected:
        std::shared_ptr<FutureStateBase> _state;
    };


    // The 'awaiter' object representing a coroutine that's awaiting a Future.
    template <typename T>
    class Waiter : public WaiterBase {
    public:
        Waiter(Future<T>& f)    :WaiterBase(f._state) { }
        Waiter(Future<T>&& f)   :WaiterBase(std::move(f._state)) { }
        
        bool await_ready()      {return state().hasValue();}
        T& await_resume()       {return state().value();}
    private:
        FutureState<T>& state() {return static_cast<FutureState<T>&>(*_state);}
    };

    // Makes Future awaitable:
    template <typename T>
    Waiter<T> operator co_await(Future<T>& cond)    {return Waiter<T>(cond);}
    template <typename T>
    Waiter<T> operator co_await(Future<T>&& cond)   {return Waiter<T>(cond);}


#pragma mark - FUTURE IMPL:


    // Implementation of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<Future<T>,FutureImpl<T>> {
    public:
        using super = CoroutineImpl<Future<T>,FutureImpl<T>>;
        using handle_type = super::handle_type;

        FutureImpl() = default;

        handle_type handle() {
            return handle_type::from_promise(*this);
        }

        T& waitForValue() {
            while (!_provider.hasValue())
                handle().resume();
            return _provider.value();
        }

        //---- C++ coroutine internal API:

        Future<T> get_return_object() {
            auto f = _provider.future();
            f.setHandle(handle());
            return f;
        }

        std::suspend_never initial_suspend()    {return {};}

        void unhandled_exception()              {_provider.setException(std::current_exception());}

        void return_value(T&& value)            {_provider.setValue(std::forward<T>(value));}

    private:
        FutureProvider<T> _provider;
    };

}
