//
// FileStream.cc
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

#include "FileStream.hh"
#include "UVInternal.hh"
#include <unistd.h>

namespace crouton {
    using namespace std;


    const int FileStream::Append = O_APPEND;
    const int FileStream::Create = O_CREAT;
    const int FileStream::ReadOnly = O_RDONLY;
    const int FileStream::ReadWrite = O_RDWR;
    const int FileStream::WriteOnly = O_WRONLY;


    class fs_request : public Request<uv_fs_s> {
    public:
        fs_request(const char* what) :Request(what) { }
        ~fs_request()       {if (await_ready()) uv_fs_req_cleanup(this);}
        void await_resume() {check(result, _what);}
    private:
    };


    FileStream::FileStream(std::string const& path, int flags, int mode)
    :_path(path)
    ,_flags(flags)
    ,_mode(mode)
    { }

    FileStream::FileStream(int fd)                     :_fd(fd) { }

//    FileStream::FileStream(FileStream&& fs)            {std::swap(_fd, fs._fd);}
//    FileStream& FileStream::operator=(FileStream&& fs) {_close(); std::swap(_fd, fs._fd); return *this;}
    FileStream::~FileStream()                          {_close();}


    Future<void> FileStream::open() {
        fs_request req("opening file");
        check(uv_fs_open(curLoop(), &req, _path.c_str(), _flags, _mode, req.callback),
                  "opening file");
        AWAIT req;

        check(req.result, "opening file");
        _fd = int(req.result);
    }


    // This is FileStream's primitive read operation.
    Future<size_t> FileStream::_preadv(const MutableBytes bufs[], size_t nbufs, int64_t offset) {
        assert(isOpen());
        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) throw invalid_argument("too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        fs_request req("reading from a file");
        check(uv_fs_read(curLoop(), &req, _fd, uvbufs, unsigned(nbufs), offset,
                         req.callback),
                  "reading from a file");
        AWAIT req;
        RETURN req.result;
    }


    Future<size_t> FileStream::preadv(const MutableBytes bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        return _preadv(bufs, nbufs, offset);
    }


    Future<ConstBytes> FileStream::_fillBuffer() {
        if (!_readBuf)
            _readBuf = make_unique<Buffer>();
        assert(_readBuf->empty());
        MutableBytes buf(_readBuf->data, Buffer::kCapacity);
        size_t n = AWAIT _preadv(&buf, 1, -1);

        _readBuf->size = uint32_t(n);
        _readBuf->used = 0;
        RETURN _readBuf->bytes();
    }


    // IStream's primitive read operation.
    Future<ConstBytes> FileStream::readNoCopy(size_t maxLen) {
        NotReentrant nr(_busy);
        if (!_readBuf || _readBuf->empty())
            (void) AWAIT _fillBuffer();
        RETURN _readBuf->read(maxLen);
    }


    Future<ConstBytes> FileStream::peekNoCopy() {
        NotReentrant nr(_busy);
        if (!_readBuf || _readBuf->empty())
            return _fillBuffer();
        else
            return _readBuf->bytes();
    }


    // This is FileStream's primitive write operation.
    Future<void> FileStream::pwritev(const ConstBytes bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        assert(isOpen());

        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) throw invalid_argument("too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        _readBuf = nullptr; // because this write might invalidate it
        fs_request req("writing to a file");
        check(uv_fs_write(curLoop(), &req, _fd, uvbufs, unsigned(nbufs), offset,
                          req.callback),
              "writing to a file");
        AWAIT req;
        RETURN;
    }


    // IStream's primitive write operation.
    Future<void> FileStream::write(ConstBytes buf) {
        return pwritev(&buf, 1, -1);
    }


    // We also override this IStream method, because it's more efficient to do it with pwritev
    // than the overridden method's multiple calls to _write.
    Future<void> FileStream::write(const ConstBytes buffers[], size_t nBuffers) {
        return pwritev(buffers, nBuffers, -1);
    }


    void FileStream::_close() {
        if (isOpen()) {
            assert(!_busy);
            _readBuf = nullptr;
            // Close synchronously, for simplicity
            uv_fs_t closeReq;
            uv_fs_close(curLoop(), &closeReq, _fd, nullptr);
            _fd = -1;
        }
    }

    Future<void> FileStream::close() {
        _close();
        return Future<void>();
    }

}
