//
// CoCondition.cc
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

#include "CoCondition.hh"
#include "Logging.hh"

namespace crouton {

    void CoCondition::notifyOne() {
        if (!_awaiters.empty())
            _awaiters.pop_front().wakeUp();
    }


    void CoCondition::notifyAll() {
        auto awaiters = std::move(_awaiters);
        for (auto &a : awaiters)
            a.wakeUp();
    }


    coro_handle CoCondition::awaiter::await_suspend(coro_handle h) noexcept {
        _suspension = Scheduler::current().suspend(h);
        LSched->debug("CoCondition {}: suspending {}",
                      (void*)this, logCoro{h});
        _cond->_awaiters.push_back(*this);
        return lifecycle::suspendingTo(h, CRTN_TYPEID(*_cond), _cond);
    }


    void CoCondition::awaiter::wakeUp() {
        LSched->debug("CoCondition {}: waking {}",
                      (void*)_cond, logCoro{_suspension.handle()});
        _suspension.wakeUp();
    }

}
