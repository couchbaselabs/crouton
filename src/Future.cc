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

    // Called by Future::await_suspend.
    // @param coro  the current coroutine that's `co_await`ing the Future
    // @return  The coroutine that should resume
    coro_handle FutureStateBase::suspend(coro_handle coro) {
        switch (State state = _state.load()) {
            case Empty: {
                // No value yet, so suspend this coroutine:
                assert(!_suspension);
                Scheduler& sched = Scheduler::current();
                _suspension = sched.suspend(coro);
                if (!_state.compare_exchange_strong(state, Waiting)) {
                    // Oops, provider set a value while I was suspending; wake immediately:
                    assert(state == Ready);
                    _suspension->wakeUp();
                }
                return sched.next();
            }
            case Waiting:
                throw std::logic_error("Another coroutine is already awaiting this Future");
            case Ready:
                // There's a value now, so continue:
                return coro;
        }
        // unreachable
        throw std::logic_error("invalid state");
    }

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
            case Ready:
                throw std::logic_error("Future already has a result");
        }
    }



    Future<void> FutureProvider<void>::future() {
        return Future<void>(_state);
    }


    Future<void> FutureImpl<void>::get_return_object() {
        auto f = _provider.future();
        f.setHandle(typedHandle());
        return f;
    }


}
