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
#include "Defer.hh"
#include "UVInternal.hh"
#include "stream_wrapper.hh"
#include <cstring>
#include <unistd.h>
#include <iostream>

namespace crouton {
    using namespace std;


#pragma mark - ISTREAM:


    Future<ConstBuf> IStream::readNoCopy(size_t maxLen) {
        NotReentrant nr(_readBusy);
        return _readNoCopy(maxLen);
    }


    void IStream::unRead(size_t len) {
        NotReentrant nr(_readBusy);
        _unRead(len);
    }


    Future<int64_t> IStream::read(size_t maxLen, void* dst) {
        NotReentrant nr(_readBusy);
        return _read(maxLen, dst);
    }


    Future<int64_t> IStream::_read(size_t maxLen, void* dst) {
        size_t bytesRead = 0;
        while (bytesRead < maxLen) {
            ConstBuf bytes = AWAIT _readNoCopy(maxLen - bytesRead);
            if (bytes.len == 0)
                break;
            ::memcpy((char*)dst + bytesRead, bytes.base, bytes.len);
            bytesRead += bytes.len;
        }
        RETURN bytesRead;
    }


    Future<void> IStream::readExactly(size_t len, void* dst) {
        int64_t bytesRead = AWAIT read(len, dst);
        if (bytesRead < len)
            check(int(UV_EOF), "reading from the network");
        RETURN;
    }


    Future<string> IStream::readString(size_t maxLen) {
        NotReentrant nr(_readBusy);
        static constexpr size_t kGrowSize = 32768;
        string data;
        size_t len = 0;
        while (len < maxLen) {
            size_t n = std::min(kGrowSize, maxLen - len);
            data.resize(len + n);
            size_t bytesRead = AWAIT _read(n, &data[len]);

            if (bytesRead < n) {
                data.resize(len + bytesRead);
                break;
            }
            len += bytesRead;
        }
        RETURN data;
    }


    Future<string> IStream::readUntil(std::string end, size_t maxLen) {
        assert(!end.empty());
        assert(maxLen >= end.size());
        NotReentrant nr(_readBusy);
        string data;
        while (data.size() < maxLen) {
            auto dataLen = data.size();
            ConstBuf peek = AWAIT _readNoCopy(maxLen - dataLen);

            // Check for a match that's split between the old and new data:
            if (!data.empty()) {
                data.append((char*)peek.base, min(end.size() - 1, peek.len));
                size_t startingAt = dataLen - std::min(end.size(), dataLen);
                if (auto found = data.find(end, startingAt); found != string::npos) {
                    // Found it:
                    found += end.size();
                    found = std::min(found, maxLen);
                    data.resize(found);
                    _unRead(peek.len - (found - dataLen));
                    break;
                } else {
                    data.resize(dataLen);
                }
            }

            // Check for a match in the new data:
            if (auto found = string_view((char*)peek.base, peek.len).find(end); found != string::npos) {
                found += end.size();
                found = std::min(found, maxLen - data.size());
                data.append((char*)peek.base, found);
                _unRead(peek.len - found);
                break;
            }

            // Otherwise append all the input data and read more:
            size_t addLen = std::min(peek.len, maxLen - data.size());
            data.append((char*)peek.base, addLen);
            _unRead(peek.len - addLen);
        }
        RETURN data;
    }


    Future<void> IStream::write(ConstBuf buf) {
        NotReentrant nr(_writeBusy);
        return _write(buf);
    }


    Future<void> IStream::write(std::string str) {
        // Use co_await to ensure `str` stays in scope until the write completes.
        AWAIT write(str.size(), str.data());
        RETURN;
    }


    Future<void> IStream::write(const ConstBuf buffers[], size_t nBuffers) {
        NotReentrant nr(_writeBusy);
        for (size_t i = 0; i < nBuffers; ++i) {
            AWAIT _write(buffers[i]);
        }
    }


    Future<void> IStream::write(std::initializer_list<ConstBuf> buffers) {
        return write(buffers.begin(), buffers.size());
    }


#pragma mark - STREAM:


    Stream::~Stream() {
        close();
    }


    void Stream::opened(std::unique_ptr<stream_wrapper> s) {
        assert(!_stream); 
        _stream = std::move(s);
        _stream->_allocCallback = [this](size_t suggested) {
            return this->_allocCallback(suggested);
        };
        _stream->_readCallback = [this](BufferRef buf, int err) {
            this->_readCallback(std::move(buf), err);
        };
    }


    Future<void> Stream::closeWrite() {
        assert(isOpen());

        Request<uv_shutdown_t> req;
        check( _stream->shutdown(&req, req.callbackWithStatus), "closing connection");
        check( AWAIT req, "closing connection" );
        RETURN;
    }


    void Stream::_close() {
        assert(!_readBusy && !_writeBusy);
        _inputBuf.reset();
        _stream.reset();
    }

    Future<void> Stream::close() {
        _close();
        return FutureProvider<void>{};
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
        size_t available = bytesAvailable();
        if (available == 0) {
            AWAIT fillInputBuf();
            available = bytesAvailable();
            if (available == 0)
                RETURN {};  // Reached EOF
        }

        // Advance _inputBuf->used and return the pointer:
        size_t n = std::min(maxLen, available);
        ConstBuf result{.base = _inputBuf->data + _inputBuf->used, .len = n};
        _inputBuf->used += n;
        RETURN result;
    }

    void Stream::_unRead(size_t len) {
        NotReentrant nr(_readBusy);
        assert(len <= _inputBuf->used);
        _inputBuf->used -= len;
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
    /// uv_buf_t. Caller must free it.
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
                _futureBuf->setValue(std::move(buf));
            } else if (err == UV_EOF || err == UV_EINVAL) {
                _futureBuf->setValue(nullptr);
            } else {
                try {
                    check(err, "reading from the network");
                } catch (...) {
                    _futureBuf->setException(std::current_exception());
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

    Future<void> Stream::write(const ConstBuf buffers[], size_t nBuffers) {
        NotReentrant nr(_writeBusy);
        assert(isOpen());
        write_request req;
        check(_stream->write(&req, (uv_buf_t*)buffers, unsigned(nBuffers),
                             req.callbackWithStatus),
              "sending to the network");
        check( AWAIT req, "sending to the network");

        RETURN;
    }


    Future<void> Stream::_write(ConstBuf buf) {
        return write(&buf, 1);
    }


    size_t Stream::tryWrite(ConstBuf buf) {
        int result = _stream->try_write((uv_buf_t*)&buf, 1);
        if (result == UV_EAGAIN)
            return 0;
        else
            check(result, "sending to the network");
        return size_t(result);
    }

}
