//
// Pipe.cc
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

#include "Pipe.hh"
#include "UVInternal.hh"

namespace crouton::io {
    using namespace std;

    pair<Pipe,Pipe> Pipe::createPair() {
        uv_file fds[2];
        check(uv_pipe(fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE), "creating pipes");
        return std::pair<Pipe,Pipe>(fds[0], fds[1]);
    }


    Pipe::Pipe(int fd) {
        // TODO: Allow Pipe to be used on a separate thread's loop, for inter-thread communication
        auto pipe = new uv_pipe_t;
        int err = uv_pipe_init(curLoop(), pipe, false);
        if (err == 0)
            err = uv_pipe_open(pipe, fd);
        if (err == 0) {
            opened((uv_stream_t*)pipe);
        } else {
            closeHandle(pipe);
            check(err, "opening a pipe");
        }
    }


    Future<void> Pipe::open() {
        return Future<void>();
    }

}
