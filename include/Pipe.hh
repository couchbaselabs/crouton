//
// Pipe.hh
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
#include "Stream.hh"
#include <utility>

struct uv_pipe_s;

namespace crouton {

    /** A bidirectional stream. Currently can only be created in ephemeral pairs. */
    class Pipe : public Stream {
    public:
        /// Creates a pair of connected Pipes.
        static std::pair<Pipe,Pipe> createPair();

        /// Creates a Pipe on an open file descriptor, which must be a pipe or Unix domain socket.
        explicit Pipe(int fd);

        Future<void> open() override;
    };

}
