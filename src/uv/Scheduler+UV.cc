//
// Scheduler+UV.cc
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
#include "UVInternal.hh"

namespace snej::coro {

    uv_loop_s* Scheduler::uvLoop() {
        assert(isCurrent());
        if (!_uvloop) {
            auto uvloop = new uv_loop_s;
            uv::check(uv_loop_init(uvloop), "initializing the event loop");
            uvloop->data = this;
            _uvloop = uvloop;
        }
        return _uvloop;
    }

    void Scheduler::useUVLoop(uv_loop_s* loop) {
        assert(isCurrent());
        if (_uvloop != loop) {
            assert(!_uvloop);
            _uvloop = loop;
            _uvloop->data = this;
        }
    }

    void Scheduler::_wait() {
        //std::cerr << "UV loop starting...\n";
        uv_run(uvLoop(), UV_RUN_DEFAULT);
        //std::cerr << "UV loop stopped\n";
    }

    void Scheduler::_wakeUp() {
        assert(_uvloop);
        uv_stop(_uvloop);
    }

    namespace uv {
        uv_loop_s* curLoop() {
            return Scheduler::current().uvLoop();
        }
    }
}

#if 0

- regular code
    - coroutine
        - suspends, into Scheduler::next(), which has no runnable coroutines
            - uv_run()
                - completion callback
                    - Scheduler::_wakeUp
                        - uv_stop()
        - Scheduler::next returns awakened coroutine

#endif
