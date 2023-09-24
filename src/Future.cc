//
// Future.cc
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

#include "Future.hh"

namespace crouton {

    // Returns true if the current state is Empty, false if it's Ready, else throws.
    bool FutureStateBase::checkEmpty() {
        switch (_state.load()) {
            case Empty:   return true;
            case Waiting: Error::raise(CroutonError::LogicError, "Another coroutine is already awaiting this Future");
            case Chained: Error::raise(CroutonError::LogicError, "This Future already has a `then(...)` callback");
            case Ready:   break;
        }
        return false;
    }


    // Atomically changes the state from Empty to Waiting or Chained.
    // Returns true on success, false if it's already Ready, otherwise throws.
    bool FutureStateBase::changeState(State newState) {
        State state = Empty;
        if (_state.compare_exchange_strong(state, newState))
            return true;
        else if (state == Ready)
            return false;
        else
            Error::raise(CroutonError::LogicError, "Race condition: two threads awaiting the same Future");
    }


    // Called by Future::await_suspend.
    // @param coro  the current coroutine that's `co_await`ing the Future
    // @return  The coroutine that should resume
    coro_handle FutureStateBase::suspend(coro_handle coro) {
        if (checkEmpty()) {
            // No value yet, so suspend this coroutine:
            assert(!_suspension);
            Scheduler& sched = Scheduler::current();
            _suspension = sched.suspend(coro);
            if (!changeState(Waiting)) {
                // Oops, provider set a value while I was suspending; wake immediately:
                _suspension->wakeUp();
            }
            return sched.next();
        } else {
            // There's a value now, so continue:
            return coro;
        }
    }


    // Chains another FutureState to this one through a `then` callback.
    void FutureStateBase::_chain(std::shared_ptr<FutureStateBase> future, ChainCallback fn) {
        bool ready = !checkEmpty();
        assert(!_chainedFuture);
        _chainedFuture = std::move(future);
        _chainedCallback = std::move(fn);
        if (ready || !changeState(Chained))
            resolveChain();
    }


    // Changes the state to Ready and notifies any waiting coroutine or chained FutureState.
    void FutureStateBase::_notify() {
        switch (_state.exchange(Ready)) {
            case Empty:
                // No one waiting yet
                break;
            case Waiting:
                // Wake the waiting coroutine:
                if (_suspension) {
                    _suspension->wakeUp();
                    _suspension = nullptr;
                }
                break;
            case Chained:
                // Resolve the chained FutureState:
                resolveChain();
                break;
            case Ready:
                Error::raise(CroutonError::LogicError, "Future already has a result");
        }
    }


    // Updates the chained FutureState based on my `then` callback.
    void FutureStateBase::resolveChain() {
        if (auto x = getError()) {
            _chainedFuture->setError(x);
        } else {
            try {
                _chainedCallback(*_chainedFuture, *this);
            } catch(...) {
                _chainedFuture->setError(Error(std::current_exception()));
            }
        }
        _chainedFuture = nullptr;
        _chainedCallback = nullptr;
    }

}
