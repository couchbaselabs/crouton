//
// Scheduler.cc
//
// 
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

#include "Scheduler.hh"
#include "uv.h"
#include <iostream>

namespace snej::coro {

    void Scheduler::_wait() {
        std::cerr << "UV loop starting...\n";
        uv_run(uv_default_loop(), UV_RUN_DEFAULT);
        std::cerr << "UV loop stopped\n";
    }

    void Scheduler::_wakeUp() {
        uv_stop(uv_default_loop());
    }

}