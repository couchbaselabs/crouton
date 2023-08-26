//
// Future.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "Future.hh"

namespace crouton {

    bool FutureStateBase::hasValue() const {
        std::unique_lock<std::mutex> lock(_mutex);
        return _hasValue;
    }

    std::coroutine_handle<> FutureStateBase::suspend(std::coroutine_handle<> coro) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_hasValue)
            return coro;
        assert(!_suspension);
        Scheduler& sched = Scheduler::current();
        _suspension = sched.suspend(coro);
        lock.unlock();
        return sched.next();
    }

    void FutureStateBase::setException(std::exception_ptr x) {
        std::unique_lock<std::mutex> lock(_mutex);
        _exception = x;
        _gotValue();
    }

    void FutureStateBase::_gotValue() {
        if (_hasValue)
            throw std::logic_error("Future's value can only be set once");
        _hasValue = true;
        if (_suspension) {
            _suspension->wakeUp();
            _suspension = nullptr;
        }
    }

    void FutureStateBase::_checkValue() {
        if (_exception)
            std::rethrow_exception(_exception);
        if (!_hasValue)
            throw std::logic_error("Future does not have a value yet");
    }


    void FutureState<void>::value() {
        std::unique_lock<std::mutex> lock(_mutex);
        _checkValue();
    }

    void FutureState<void>::setValue() {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_hasValue)
            throw std::logic_error("Future's value can only be set once");
        _gotValue();
    }



    Future<void> FutureProvider<void>::future()                      {return Future<void>(_state);}
    FutureProvider<void>::operator Future<void>()                    {return future();}


    void FutureImpl<void>::waitForValue() {
        Scheduler::current().runUntil([=]{ return _provider.hasValue(); });
        return _provider.value();
    }

    Future<void> FutureImpl<void>::get_return_object() {
        auto f = _provider.future();
        f.setHandle(handle());
        return f;
    }

}
