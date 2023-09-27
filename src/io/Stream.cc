//
// Stream.cc
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

#include "Stream.hh"
#include "UVInternal.hh"

namespace crouton::io {
    using namespace std;


    Stream::Stream() = default;

    Stream::~Stream() {
        close();
    }


    void Stream::opened(uv_stream_t *stream) {
        assert(!_stream);
        _stream = stream;
        _stream->data = this;
    }


    Future<void> Stream::closeWrite() {
        assert(isOpen());

        Request<uv_shutdown_t> req("closing connection");
        check( uv_shutdown(&req, _stream, req.callbackWithStatus), "closing connection");
        AWAIT req;
        RETURN noerror;
    }


    void Stream::_close() {
        assert(!_readBusy);
        _inputBuf.reset();
        closeHandle(_stream);
    }

    Future<void> Stream::close() {
        _close();
        return Future<void>();
    }


#pragma mark - READING:


    size_t Stream::bytesAvailable() const {
        return _inputBuf ? _inputBuf->available() : 0;
    }


    bool Stream::isReadable() const {
        return _stream && (bytesAvailable() > 0 || uv_is_readable(_stream));
    }

    
    Future<ConstBytes> Stream::readNoCopy(size_t maxLen) {
        assert(isOpen());
        NotReentrant nr(_readBusy);
        if (_inputBuf && !_inputBuf->empty()) {
            // Advance _inputBuf->used and return the pointer:
            return _inputBuf->read(maxLen);
        } else {
            return fillInputBuf().then([this,maxLen](ConstBytes bytes) -> ConstBytes {
                if (_inputBuf)
                    return _inputBuf->read(maxLen);
                else
                    return ConstBytes{};  // Reached EOF
            });
        }
    }


    Future<ConstBytes> Stream::peekNoCopy() {
        assert(isOpen());
        NotReentrant nr(_readBusy);
        if (!_inputBuf || _inputBuf->empty())
            return fillInputBuf();
        else
            return _inputBuf->bytes();
    }


    /// Low-level read method that ensures there is data to read in `_inputBuf`.
    Future<ConstBytes> Stream::fillInputBuf() {
        assert(isOpen());
        assert(_readBusy);
        if (_inputBuf && _inputBuf->available() == 0) {
            _spare.emplace_back(std::move(_inputBuf));
            _inputBuf.reset();
        }
        if (!_inputBuf) {
            // Reload the input buffer from the socket:
            _inputBuf = AWAIT readBuf();
        }
        RETURN _inputBuf ? _inputBuf->bytes() : ConstBytes{};
    }


    /// Reads once from the uv_stream and returns the result as a BufferRef.
    Future<BufferRef> Stream::readBuf() {
        assert(isOpen());
        assert(!_futureBuf);
        if (!_input.empty()) {
            // We have an already-read buffer available; return it:
            Future<BufferRef> result(std::move(_input[0]));
            _input.erase(_input.begin());
            return result;
        } else if (auto err = _readError) {
            // The last async read resulted in an error; return it to the caller:
            _readError = 0;
            if (err == UV_EOF || err == UV_EINVAL)
                return BufferRef();
            else
                return Error(UVError(err), "reading from the network");
        } else {
            // Start an async read:
            read_start();
            _futureBuf = make_shared<FutureState<BufferRef>>();
            return Future<BufferRef>(_futureBuf);
        }
    }


#pragma mark - LOW-LEVEL (LIBUV) READING:


    /// Triggers an async read by libuv.
    void Stream::read_start() {
        if (!_reading) {
            auto alloc = [](uv_handle_t* h, size_t suggested, uv_buf_t* uvbuf) {
                static_cast<Stream*>(h->data)->_allocCallback(suggested, uvbuf);
            };
            auto read = [](uv_stream_t* h, intptr_t nread, const uv_buf_t* uvbuf) {
                static_cast<Stream*>(h->data)->_readCallback(nread, uvbuf);
            };
            check(uv_read_start(_stream, alloc, read), "reading from the network");
            _reading = true;
        }
    }


    /// libuv is asking me to allocate a buffer.
    void Stream::_allocCallback(size_t suggested, uv_buf_t* uvbuf) {
        // Recycle or allocate _readingBuf:
        if (_spare.empty()) {
            _readingBuf = std::make_unique<Buffer>();
        } else {
            _readingBuf = std::move(_spare.back());
            _spare.pop_back();
        }
        // Point the input uvbuf to it:
        uvbuf->base = (char*)_readingBuf->data;
        uvbuf->len = Buffer::kCapacity;
    }


    /// libuv is notifying me that it put data into an allocated buffer.
    void Stream::_readCallback(intptr_t nread, const uv_buf_t* uvbuf) {
        int err;
        if (nread > 0) {
            // A successful read into _readingBuf:
            assert(size_t(nread) <= Buffer::kCapacity);
            assert(uvbuf->base == (char*)_readingBuf->data);
            _readingBuf->size = uint32_t(nread);
            _readingBuf->used = 0;
            err = 0;
        } else {
            // Or an error.
            assert(nread != 0);
            err = int(nread);
        }

        if (_futureBuf) {
            // Fulfil the Future from the latest read() call:
            if (err == 0)
                _futureBuf->setResult(std::move(_readingBuf));
            else if (err == UV_EOF || err == UV_EINVAL)
                _futureBuf->setResult(nullptr);
            else
                _futureBuf->setResult(UVError(err));
            _futureBuf = nullptr;
        } else {
            // If this is an unrequested read, queue it up for later:
            if (err == 0)
                _input.emplace_back(std::move(_readingBuf));
            else
                _readError = err;
            // Stop reading so that I don't get spammed with data:
            uv_read_stop(_stream);
            _reading = false;
       }
    }


#pragma mark - WRITING:


    bool Stream::isWritable() const {
        return _stream && uv_is_writable(_stream);
    }

    Future<void> Stream::write(const ConstBytes bufs[], size_t nbufs) {
        assert(isOpen());

        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs)
            RETURN Error(CroutonError::InvalidArgument, "too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        write_request req("sending to the network");
        check(uv_write(&req, _stream, uvbufs, unsigned(nbufs), req.callbackWithStatus),
              "sending to the network");
        AWAIT req;
        RETURN noerror;
    }


    Future<void> Stream::write(ConstBytes buf) {
        return write(&buf, 1);
    }


    size_t Stream::tryWrite(ConstBytes buf) {
        uv_buf_t uvbuf(buf);
        int result = uv_try_write(_stream, &uvbuf, 1);
        if (result == UV_EAGAIN)
            return 0;
        else
            check(result, "sending to the network");
        return size_t(result);
    }

}
