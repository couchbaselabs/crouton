//
// Select.hh
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
#include "Awaitable.hh"
#include "Scheduler.hh"

#include <array>
#include <bitset>
#include <functional>
#include <initializer_list>

namespace crouton {

    /** Enables `co_await`ing multiple (up to 8) ISelectable values in parallel.
        After enabling the desired objects, `co_await` on the Select will return the index of the
        lowest-numbered enabled source you can co `co_await` without blocking.
        Example:
        ```
            Select sel { &gen0, &gen1 };
            sel.enable(0);
            sel.enable(1);
            switch (AWAIT sel) {
                case 0: {auto val = AWAIT gen0; ... break;}
                case 1: {auto val = AWAIT gen1; ... break;}
            }
        ``` */
    class Select {
    public:
        /// Constructs a Select that will watch the given list of ISelectable objects.
        Select(std::initializer_list<ISelectable*> sources);
        ~Select();

        /// Begins watching the source at the given index.
        /// @note  Once a source has been returned from `co_await` it needs to be re-enabled
        ///        before it can be selected again.
        /// @note  If no sources are enabled, `co_await` will immediately return -1.
        void enable(unsigned index);

        /// Begins watching each source.
        void enable();

        //---- Awaitable methods. `co_await` returns the index of a ready source.
        bool await_ready() noexcept     {return _ready.any() || _enabled.none();}
        coro_handle await_suspend(coro_handle h);
        int await_resume();

    private:
        static constexpr size_t kMaxSources = 8;   // can be increased if necessary

        void notify(unsigned index);

        std::array<ISelectable*,kMaxSources>    _sources;
        std::bitset<kMaxSources>                _enabled, _ready;
        Suspension                              _suspension;
    };





}
