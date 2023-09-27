//
// Task.hh
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
#include "Scheduler.hh"
#include <atomic>

namespace crouton {
    class TaskImpl;


    /** Return type for a coroutine that doesn't return a value, just runs indefinitely.
        Unlike the base class CoroutineHandle, it does not destroy the coroutine handle in its
        destructor. */
    class Task : public Coroutine<TaskImpl> {
    public:
        Task(Task&&) = default;

        /// Returns true as long as the task coroutine is still running.
        bool alive()                        {return _shared->alive;}

        /// Lets the task coroutine know it should stop. Its next `co_yield` will return false.
        void interrupt()                    {_shared->interrupt = true;}

    protected:
        friend class Scheduler;
    private:
        friend class TaskImpl;

        struct shared {
            std::atomic<bool> alive = true;
            std::atomic<bool> interrupt = false;
        };

        Task(handle_type h, std::shared_ptr<shared> s)
        :Coroutine<TaskImpl>(h)
        ,_shared(std::move(s))
        { }

        std::shared_ptr<shared> _shared;
    };



    class TaskImpl : public CoroutineImpl<TaskImpl, false> {
    public:
        ~TaskImpl() { }

        Task get_return_object() {
            _shared = std::make_shared<shared>();
            return Task(typedHandle(), _shared);
        }

        auto initial_suspend() {
            struct sus : public CORO_NS::suspend_always {
                void await_suspend(coro_handle h) {
                    Scheduler::current().schedule(h);
                }
            };
            return sus{};
        }

        // `co_yield` returns a bool: true to keep running, false if Task has been interrupted.
        auto yield_value(bool) {
            struct yielder : public Yielder {
                explicit yielder(coro_handle myHandle, std::shared_ptr<shared> shared)
                :Yielder(myHandle), _shared(std::move(shared)) { }
                [[nodiscard]] bool await_resume() const noexcept {
                    Yielder::await_resume();
                    return !_shared->interrupt;
                }
                std::shared_ptr<shared> _shared;
            };
            return yielder(handle(), _shared);
        }

        void return_void() {
            _shared->alive = false;
            lifecycle::returning(handle());
        }

        SuspendFinal<true> final_suspend() noexcept {
            _shared->alive = false;
            return {};
        }

    private:
        using shared = Task::shared;
        std::shared_ptr<shared> _shared;
    };
}
