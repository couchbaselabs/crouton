//
// CoCondition.hh
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
#include "util/LinkedList.hh"
#include "Scheduler.hh"

#include <concepts>
#include <optional>

namespace crouton {

    /** A cooperative condition variable. A coroutine that `co_await`s it will block until
        something calls `notify` or `notifyAll`. If there are multiple coroutines blocked,
        `notify` wakes one, while `notifyAll` wakes all of them.
        @warning  Not thread-safe, despite the name! */
    class CoCondition {
    public:
        CoCondition() = default;
        CoCondition(CoCondition&&) noexcept = default;
        CoCondition& operator=(CoCondition&&) noexcept = default;
        ~CoCondition() {precondition(_awaiters.empty());}

        void notifyOne();

        void notifyAll();

        struct awaiter : public CORO_NS::suspend_always, private util::Link {
            awaiter(CoCondition* cond) :_cond(cond) { }

            coro_handle await_suspend(coro_handle h) noexcept;

        private:
            friend class CoCondition;
            friend class util::LinkList;
            void wakeUp();

            CoCondition* _cond;
            Suspension   _suspension;
        };

        awaiter operator co_await() {return awaiter(this);}

    private:
        void remove(awaiter* a);

        util::LinkedList<awaiter> _awaiters;
    };


#pragma mark - BLOCKER:


    /** A simpler way to await a future event. A coroutine that `co_await`s a Blocker will block
        until something calls the Blocker's `notify` method. This provides an easy way to turn
        a completion-callback based API into a coroutine-based one: create a Blocker, start
        the operation, then `co_await` the Blocker. In the completion callback, call `notify`.

        The value you pass to `notify` will be returned from the `co_await`.

        @note  Blocker is thread-safe. `notify()` can safely be called from a different thread.

        Blocker supports only one waiting coroutine. If you need more, use a CoCondition. */
    template <typename T>
    class Blocker {
    public:
        bool await_ready() noexcept     {return _value.has_value();}

        coro_handle await_suspend(coro_handle h) noexcept {
            _suspension = Scheduler::current().suspend(h);
            return lifecycle::suspendingTo(h, typeid(*this), this);
        }

        T await_resume() noexcept {
            T result = std::move(_value).value();
            _value = std::nullopt;
            return result;
        }

        template <typename U>
        void notify(U&& val) {
            assert(!_value);
            _value.emplace(std::forward<U>(val));
            _suspension.wakeUp();
        }

    private:
        Suspension _suspension;
        std::optional<T> _value;
    };


    template <>
    class Blocker<void> {
    public:
        bool await_ready() noexcept     {return _hasValue;}

        coro_handle await_suspend(coro_handle h) noexcept {
            assert(!_suspension);
            _suspension = Scheduler::current().suspend(h);
            return lifecycle::suspendingTo(h, typeid(*this), this);
        }

        void await_resume() noexcept    {_hasValue = false;}

        void notify()                   {_hasValue = true; _suspension.wakeUp();}

    private:
        Suspension  _suspension;
        bool        _hasValue = false;
    };

}
