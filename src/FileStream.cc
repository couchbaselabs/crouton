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
#include "stream_wrapper.hh"
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
        ~fs_request()       {if (await_ready()) uv_fs_req_cleanup(this);}
        int await_resume()  {return int(result);}
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
        fs_request req;
        check(uv_fs_open(curLoop(), &req, _path.c_str(), _flags, _mode, req.callback),
                  "opening file");
        AWAIT req;

        check(req.result, "opening file");
        _fd = int(req.result);
    }


    // This is FileStream's primitive read operation.
    Future<size_t> FileStream::_preadv(const MutableBuf bufs[], size_t nbufs, int64_t offset) {
        assert(isOpen());
        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) throw invalid_argument("too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        fs_request req;
        check(uv_fs_read(curLoop(), &req, _fd, uvbufs, unsigned(nbufs), offset,
                         req.callback),
                  "reading from a file");
        AWAIT req;

        check(req.result, "reading from a file");
        RETURN req.result;
    }


    Future<size_t> FileStream::preadv(const MutableBuf bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        return _preadv(bufs, nbufs, offset);
    }


    // We also override this IStream method, because it's more efficient to do it with preadv;
    // it saves a memcpy.
    Future<size_t> FileStream::read(MutableBuf buf) {
        return preadv(&buf, 1, -1);
    }


    // IStream's primitive read operation.
    [[nodiscard]] Future<ConstBuf> FileStream::_readNoCopy(size_t maxLen) {
        NotReentrant nr(_busy);
        if (!_readBuf)
            _readBuf = make_unique<Buffer>();
        if (_readBuf->empty()) {
            MutableBuf buf(_readBuf->data, Buffer::kCapacity);
            _readBuf->size = uint32_t(AWAIT _preadv(&buf, 1, -1));
            _readBuf->used = 0;
        }
        RETURN _readBuf->read(maxLen);
    }


    // This is FileStream's primitive write operation.
    Future<void> FileStream::pwritev(const ConstBuf bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        assert(isOpen());

        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) throw invalid_argument("too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        _readBuf = nullptr; // because this write might invalidate it
        fs_request req;
        check(uv_fs_write(curLoop(), &req, _fd, uvbufs, unsigned(nbufs), offset,
                          req.callback),
              "writing to a file");
        AWAIT req;

        check(req.result, "writing to a file");
        RETURN;
    }


    // IStream's primitive write operation.
    Future<void> FileStream::_write(ConstBuf buf) {
        return pwritev(&buf, 1, -1);
    }


    // We also override this IStream method, because it's more efficient to do it with pwritev
    // than the overridden method's multiple calls to _write.
    Future<void> FileStream::write(const ConstBuf buffers[], size_t nBuffers) {
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
