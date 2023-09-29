//
// Awaitable.hh
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
#include "Result.hh"

#include <functional>

namespace crouton {

    /** Pure-virtual interface declaring the coroutine methods needed to support `co_await`
        returning type `T`. */
    template <typename T>
    class IAwaitable {
    public:
        virtual ~IAwaitable() = default;

        virtual bool await_ready() = 0;
        virtual coro_handle await_suspend(coro_handle cur) = 0;
        virtual T await_resume() =0;
    };



    /** Pure-virtual interface for awaitable objects that can notify via a callback
        when they're ready, i.e. when `co_await` will not block.
        Generator and Future implement this. */
    class ISelectable {
    public:
        using OnReadyFn = std::function<void()>;

        virtual ~ISelectable() = default;
        virtual void onReady(OnReadyFn fn) =0;
    };



    /** A type of Awaitable (plus ISelectable) that guarantees to produce:
        - zero or more `T`s, then
        - either empty/`noerror` (completion) or an `Error` (failure.)
        `Generator` is the canonical example. */
    template <typename T>
    class ISeries : public IAwaitable<Result<T>>, public ISelectable { };
}
