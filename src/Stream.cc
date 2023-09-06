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
#include <unistd.h>

namespace crouton {
    using namespace std;


    // Wrapper around a uv_stream_t.
    // TODO: Merge this into the Stream class; it's not useful on its own anymore.
    struct uv_stream_wrapper {
        explicit uv_stream_wrapper(uv_stream_t *stream) :_stream(stream) {_stream->data = this;}

        ~uv_stream_wrapper() {closeHandle(_stream);}

        using AllocCallback = std::function<BufferRef(size_t suggestedSize)>;
        using ReadCallback = std::function<void(BufferRef, int err)>;

        AllocCallback _allocCallback;
        ReadCallback  _readCallback;

        int read_start() {
            auto alloc = [](uv_handle_t* h, size_t suggested, uv_buf_t* uvbuf) {
                auto stream = (uv_stream_wrapper*)h->data;
                stream->alloc(suggested, uvbuf);
            };
            auto read = [](uv_stream_t* h, ssize_t nread, const uv_buf_t* uvbuf) {
                auto stream = (uv_stream_wrapper*)h->data;
                stream->read(nread, uvbuf);
            };
            return uv_read_start(_stream, alloc, read);
        }

        int read_stop() {
            return uv_read_stop(_stream);
        }

        int write(uv_write_t *req, const uv_buf_t bufs[], unsigned nbufs, uv_write_cb cb) {
            return uv_write(req, _stream, bufs, nbufs, cb);
        }

        int try_write(const uv_buf_t bufs[], unsigned nbufs) {
            return uv_try_write(_stream, bufs, nbufs);
        }

        bool is_readable() {return uv_is_readable(_stream);}
        bool is_writable() {return uv_is_writable(_stream);}

        int shutdown(uv_shutdown_t* req, uv_shutdown_cb cb) {
            return uv_shutdown(req, _stream, cb);
        }

    private:
        void alloc(size_t suggested, uv_buf_t* uvbuf) {
            _readingBuf = _allocCallback(suggested);
            uvbuf->base = (char*)_readingBuf->data;
            uvbuf->len = Buffer::kCapacity;
        }

        void read(ssize_t nread, const uv_buf_t* uvbuf) {
            assert(nread != 0);
            if (nread > 0) {
                assert(size_t(nread) <= Buffer::kCapacity);
                assert(uvbuf->base == (char*)_readingBuf->data);
                _readingBuf->size = uint32_t(nread);
                _readingBuf->used = 0;
                nread = 0;
            }
            _readCallback(std::move(_readingBuf), int(nread));
        }

        std::vector<BufferRef> _input, _spare;
        BufferRef _readingBuf;
        uv_stream_t* _stream;
    };


#pragma mark - STREAM:
    

    Stream::Stream() = default;

    Stream::~Stream() {
        close();
    }


    void Stream::opened(uv_stream_t *stream) {
        assert(!_stream);
        _stream = make_unique<uv_stream_wrapper>(stream);
        _stream->_allocCallback = [this](size_t suggested) {
            return this->_allocCallback(suggested);
        };
        _stream->_readCallback = [this](BufferRef buf, int err) {
            this->_readCallback(std::move(buf), err);
        };
    }


    Future<void> Stream::closeWrite() {
        assert(isOpen());

        Request<uv_shutdown_t> req("closing connection");
        check( _stream->shutdown(&req, req.callbackWithStatus), "closing connection");
        AWAIT req;
    }


    void Stream::_close() {
        assert(!_readBusy && !_writeBusy);
        _inputBuf.reset();
        _stream.reset();
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
        return _stream && (bytesAvailable() > 0 || _stream->is_readable());
    }

    
    Future<ConstBuf> Stream::_readNoCopy(size_t maxLen) {
        assert(isOpen());
        NotReentrant nr(_readBusy);
        if (!_inputBuf || _inputBuf->empty()) {
            AWAIT fillInputBuf();
            if (!_inputBuf || _inputBuf->empty())
                RETURN {};  // Reached EOF
        }

        // Advance _inputBuf->used and return the pointer:
        RETURN _inputBuf->read(maxLen);
    }


    /// Low-level read method that ensures there is data to read in `_inputBuf`.
    Future<void> Stream::fillInputBuf() {
        assert(isOpen());
        assert(_readBusy);
        if (_inputBuf && _inputBuf->available() == 0)
            _spare.emplace_back(std::move(_inputBuf));

        if (!_inputBuf) {
            // Reload the input buffer from the socket:
            _inputBuf = AWAIT readBuf();
        }
    }


    /// The base read method. Reads once from the uv_stream and returns the result as a
    /// BufferRef. Caller must free it.
    Future<BufferRef> Stream::readBuf() {
        assert(isOpen());
        assert(!_futureBuf);
        if (!_input.empty()) {
            Future<BufferRef> result(std::move(_input[0]));
            _input.erase(_input.begin());
            return result;
        } else if (auto err = _readError) {
            _readError = 0;
            if (err == UV_EOF || err == UV_EINVAL)
                return BufferRef();
            else
                throw UVError("reading from the network", err);
        } else {
            check(_stream->read_start(), "reading from the network");
            _futureBuf.emplace();
            return *_futureBuf;
        }
    }


    BufferRef Stream::_allocCallback(size_t) {
        if (_spare.empty()) {
            return std::make_unique<Buffer>();
        } else {
            auto buf = std::move(_spare.back());
            _spare.pop_back();
            return buf;
        }
    }

    void Stream::_readCallback(BufferRef buf, int err) {
        if (_futureBuf) {
            if (err == 0) {
                _futureBuf->setResult(std::move(buf));
            } else if (err == UV_EOF || err == UV_EINVAL) {
                _futureBuf->setResult(nullptr);
            } else {
                try {
                    check(err, "reading from the network");
                } catch (...) {
                    _futureBuf->setResult(std::current_exception());
                }
            }
            _futureBuf = nullopt;
        } else {
            if (err) {
                _readError = err;
            } else {
                _input.emplace_back(std::move(buf));
            }
        }
        _stream->read_stop();
    }


#pragma mark - WRITING:


    bool Stream::isWritable() const {
        return _stream && _stream->is_writable();
    }

    Future<void> Stream::write(const ConstBuf bufs[], size_t nbufs) {
        NotReentrant nr(_writeBusy);
        assert(isOpen());

        static constexpr size_t kMaxBufs = 8;
        if (nbufs > kMaxBufs) throw invalid_argument("too many bufs");
        uv_buf_t uvbufs[kMaxBufs];
        for (size_t i = 0; i < nbufs; ++i)
            uvbufs[i] = uv_buf_t(bufs[i]);

        write_request req("sending to the network");
        check(_stream->write(&req, uvbufs, unsigned(nbufs), req.callbackWithStatus),
              "sending to the network");
        AWAIT req;
    }


    Future<void> Stream::_write(ConstBuf buf) {
        return write(&buf, 1);
    }


    size_t Stream::tryWrite(ConstBuf buf) {
        uv_buf_t uvbuf(buf);
        int result = _stream->try_write(&uvbuf, 1);
        if (result == UV_EAGAIN)
            return 0;
        else
            check(result, "sending to the network");
        return size_t(result);
    }

}
