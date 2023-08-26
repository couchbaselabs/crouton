//
// Stream.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "Stream.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include "stream_wrapper.hh"
#include <unistd.h>
#include <iostream>

namespace crouton {
    using namespace std;


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


    Future<void> Stream::shutdown() {
        assert(isOpen());

        Request<uv_shutdown_t> req;
        check( _stream->shutdown(&req, req.callbackWithStatus), "closing connection");
        check( AWAIT req, "closing connection" );
        RETURN;
    }


    void Stream::close() {
        assert(!_readBusy && !_writeBusy);
        _inputBuf.reset();
        _stream.reset();
    }


#pragma mark - READING:


    size_t Stream::bytesAvailable() const {
        return _inputBuf ? _inputBuf->available() : 0;
    }


    bool Stream::isReadable() const {
        return _stream && (bytesAvailable() > 0 || _stream->is_readable());
    }

    Future<void> Stream::readExactly(size_t len, void* dst) {
        int64_t bytesRead = AWAIT read(len, dst);
        if (bytesRead < len)
            check(int(UV_EOF), "reading from the network");
        RETURN;
    }


    Future<string> Stream::readUntil(std::string end, size_t maxLen) {
        NotReentrant nr(_readBusy);
        string data;
        for(;;) {
            size_t available = bytesAvailable();
            if (available == 0) {
                // Read from the socket:
                AWAIT _read();
                available = bytesAvailable();
                if (available == 0)
                    check(int(UV_EOF), "reading");  // Failure: Reached EOF
            }
            auto newBytes = _inputBuf->data + _inputBuf->used;

            // Check for a match that's split between the old and new data:
            if (!data.empty()) {
                auto dataLen = data.size();
                data.append(newBytes, min(end.size() - 1, available));
                size_t startingAt = dataLen - std::min(end.size(), dataLen);
                if (auto found = data.find(end, startingAt); found != string::npos) {
                    found += end.size();
                    found = std::min(found, maxLen);
                    data.resize(found);
                    _inputBuf->used += found - dataLen;
                    break;
                } else {
                    data.resize(dataLen);
                }
            }

            // Check for a match in the new data:
            if (auto found = string_view(newBytes, available).find(end); found != string::npos) {
                found += end.size();
                found = std::min(found, maxLen - data.size());
                data.append(newBytes, found);
                _inputBuf->used += found;
                break;
            }

            // Otherwise append all the input data and read more:
            size_t addLen = std::min(available, maxLen - data.size());
            data.append(newBytes, addLen);
            _inputBuf->used += addLen;
            if (data.size() == maxLen)
                break;
            assert(_inputBuf->used == _inputBuf->length);
        }
        RETURN data;
    }


    Future<string> Stream::read(size_t maxLen) {
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


    Future<int64_t> Stream::read(size_t maxLen, void* dst) {
        NotReentrant nr(_readBusy);
        return _read(maxLen, dst);
    }

    Future<int64_t> Stream::_read(size_t maxLen, void* dst) {
        size_t bytesRead = 0;
        while (bytesRead < maxLen) {
            WriteBuf bytes = AWAIT _readNoCopy(maxLen - bytesRead);
            if (bytes.len == 0)
                break;
            ::memcpy((char*)dst + bytesRead, bytes.base, bytes.len);
            bytesRead += bytes.len;
        }
        RETURN bytesRead;
    }


    Future<WriteBuf> Stream::readNoCopy(size_t maxLen) {
        NotReentrant nr(_readBusy);
        return _readNoCopy(maxLen);
    }


    Future<WriteBuf> Stream::_readNoCopy(size_t maxLen) {
        assert(isOpen());
        size_t available = bytesAvailable();
        if (available == 0) {
            AWAIT _read();
            available = bytesAvailable();
            if (available == 0)
                RETURN {};  // Reached EOF
        }

        // Advance _inputBuf->used and return the pointer:
        size_t n = std::min(maxLen, available);
        WriteBuf result{.base = _inputBuf->data + _inputBuf->used, .len = n};
        _inputBuf->used += n;
        RETURN result;
    }


    /// Low-level read method that ensures there is data to read in `_inputBuf`.
    Future<void> Stream::_read() {
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

    Future<void> Stream::write(std::string str) {
        // Use co_await to ensure `str` stays in scope until the write completes.
        AWAIT write(str.size(), str.data());
        RETURN;
    }

    Future<void> Stream::write(std::initializer_list<WriteBuf> buffers) {
        return write(buffers.begin(), buffers.size());
    }

    Future<void> Stream::write(size_t len, const void* src) {
        WriteBuf buf{src, len};
        return write(&buf, 1);
    }

    Future<void> Stream::write(const WriteBuf buffers[], size_t nBuffers) {
        NotReentrant nr(_writeBusy);
        assert(isOpen());
        write_request req;
        check(_stream->write(&req, (uv_buf_t*)buffers, unsigned(nBuffers),
                             req.callbackWithStatus),
              "sending to the network");
        check( AWAIT req, "sending to the network");

        RETURN;
    }


    size_t Stream::tryWrite(WriteBuf buf) {
        int result = _stream->try_write((uv_buf_t*)&buf, 1);
        if (result == UV_EAGAIN)
            return 0;
        else
            check(result, "sending to the network");
        return size_t(result);
    }

}
