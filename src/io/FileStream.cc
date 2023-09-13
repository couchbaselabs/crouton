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

#include "io/FileStream.hh"
#include "UVInternal.hh"

namespace crouton::io {
    using namespace std;


    const int FileStream::Append = O_APPEND;
    const int FileStream::Create = O_CREAT;
    const int FileStream::ReadOnly = O_RDONLY;
    const int FileStream::ReadWrite = O_RDWR;
    const int FileStream::WriteOnly = O_WRONLY;


    // Subclass of libuv file request that resolves to a Future<size_t>.
    class fs_request : public uv_fs_s {
    public:
        fs_request(const char* what)        :_what(what) { }

        void check(int r) {
            if (r < 0)
                _futureP->setError(Error(UVError{r}, _what));
        }

        static void callback(uv_fs_s *req) {
            auto self = static_cast<fs_request*>(req);
            if (self->result < 0)
                self->check(int(self->result));
            else
                self->_futureP->setResult(size_t(self->result));
            uv_fs_req_cleanup(req);
            delete self;
        }

        Future<size_t> future() {
            Future<size_t> future(_futureP);
            if (_futureP->getError())
                delete this;
            return future;
        }

    private:
        const char* _what;                  // must point to a string constant (never freed)
        FutureProvider<size_t> _futureP = make_shared<FutureState<size_t>>();
    };


    FileStream::FileStream(string const& path, int flags, int mode)
    :_path(path)
    ,_flags(flags)
    ,_mode(mode)
    { }

    FileStream::FileStream(int fd)                     :_fd(fd) { }
    FileStream::~FileStream()                          {_close();}


    Future<void> FileStream::open() {
        auto req = new fs_request("opening file");
        req->check(uv_fs_open(curLoop(), req, _path.c_str(), _flags, _mode, req->callback));
        return req->future().then([this](ssize_t result) {_fd = int(result);});
    }


    // This is FileStream's primitive read operation.
    Future<size_t> FileStream::_preadv(const MutableBytes bufs[], size_t nbufs, int64_t offset) {
        assert(isOpen());
        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) 
            return Error(CroutonError::InvalidArgument, "too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        auto req = new fs_request("reading from a file");
        req->check(uv_fs_read(curLoop(), req, _fd, uvbufs, unsigned(nbufs), offset,
                              req->callback));
        return req->future();
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
        size_t n = TRY_AWAIT(_preadv(&buf, 1, -1));

        _readBuf->size = uint32_t(n);
        _readBuf->used = 0;
        RETURN _readBuf->bytes();
    }


    // IStream's primitive read operation.
    Future<ConstBytes> FileStream::readNoCopy(size_t maxLen) {
        NotReentrant nr(_busy);
        if (_readBuf && !_readBuf->empty())
            return _readBuf->read(maxLen);
        else {
            return _fillBuffer().then([this,maxLen](ConstBytes) {
                return _readBuf->read(maxLen);
            });
        }
    }


    Future<ConstBytes> FileStream::peekNoCopy() {
        NotReentrant nr(_busy);
        if (_readBuf && !_readBuf->empty())
            return _readBuf->bytes();
        else
            return _fillBuffer();
    }


    // This is FileStream's primitive write operation.
    Future<void> FileStream::pwritev(const ConstBytes bufs[], size_t nbufs, int64_t offset) {
        NotReentrant nr(_busy);
        assert(isOpen());

        _readBuf = nullptr; // because this write might invalidate it

        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) 
            return Error(CroutonError::InvalidArgument, "too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        auto req = new fs_request("writing to a file");
        req->check(uv_fs_write(curLoop(), req, _fd, uvbufs, unsigned(nbufs), offset,
                               req->callback));
        return req->future().then([](size_t) { });
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
