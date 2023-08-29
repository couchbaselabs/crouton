//
// Pipe.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include "Stream.hh"

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
