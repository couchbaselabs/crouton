//
// AsyncFile.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "AsyncFile.hh"
#include "UVInternal.hh"
#include <unistd.h>

namespace crouton {
    using namespace std;


#pragma mark - FILE STREAM:


    const int FileStream::Append = O_APPEND;
    const int FileStream::Create = O_CREAT;
    const int FileStream::ReadOnly = O_RDONLY;
    const int FileStream::ReadWrite = O_RDWR;
    const int FileStream::WriteOnly = O_WRONLY;


    class fs_request : public Request<uv_fs_s> {
    public:
        ~fs_request()       {if (await_ready()) uv_fs_req_cleanup(this);}
        int await_resume()  {return int(result);}
    private:
    };


    Future<FileStream> FileStream::open(std::string const& path, int flags, int mode) {
        fs_request req;
        check(uv_fs_open(curLoop(), &req, path.c_str(), flags, mode, req.callback),
                  "opening file");
        AWAIT req;

        check(req.result, "opening file");
        RETURN FileStream(int(req.result));
    }


    Future<size_t> FileStream::preadv(const MutableBuf bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        assert(isOpen());
        fs_request req;
        check(uv_fs_read(curLoop(), &req, _fd, (uv_buf_t*)bufs, unsigned(nbufs), offset,
                         req.callback),
                  "reading from a file");
        AWAIT req;

        check(req.result, "reading from a file");
        RETURN req.result;
    }

    
    Future<void> FileStream::pwritev(const ConstBuf bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        assert(isOpen());
        fs_request req;
        check(uv_fs_write(curLoop(), &req, _fd, (uv_buf_t*)bufs, unsigned(nbufs), offset,
                              req.callback),
                  "writing to a file");
        AWAIT req;
        
        check(req.result, "writing to a file");
        RETURN;
    }
    
    
    void FileStream::close() {
        if (isOpen()) {
            assert(!_busy);
            // Close synchronously, for simplicity
            uv_fs_t closeReq;
            uv_fs_close(curLoop(), &closeReq, _fd, nullptr);
            _fd = -1;
        }
    }
    
}
