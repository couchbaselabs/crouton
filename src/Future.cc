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

    bool FutureStateBase::hasValue() const {
        std::unique_lock<std::mutex> lock(_mutex);
        return _hasValue;
    }

    coro_handle FutureStateBase::suspend(coro_handle coro) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_hasValue)
            return coro;
        assert(!_suspension);
        Scheduler& sched = Scheduler::current();
        _suspension = sched.suspend(coro);
        lock.unlock();
        return sched.next();
    }

    void FutureStateBase::_gotValue() {
        _hasValue = true;
        if (_suspension) {
            _suspension->wakeUp();
            _suspension = nullptr;
        }
    }



    Future<void> FutureProvider<void>::future()                      {return Future<void>(_state);}


    void FutureImpl<void>::waitForValue() {
        Scheduler::current().runUntil([this]{ return _provider.hasValue(); });
        return _provider.value();
    }

    Future<void> FutureImpl<void>::get_return_object() {
        auto f = _provider.future();
        f.setHandle(handle());
        return f;
    }

}
