//
// Pipe.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "Pipe.hh"
#include "UVInternal.hh"
#include "stream_wrapper.hh"

namespace crouton {
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
            opened(make_unique<uv_stream_wrapper>(pipe));
        } else {
            closeHandle(pipe);
            check(err, "opening a pipe");
        }
    }

}
