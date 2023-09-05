//
// stream_wrapper.hh
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

#pragma once
#include "UVInternal.hh"
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace crouton {

    /** A data buffer used by stream_wrapper and Stream. */
    struct Buffer {
        static constexpr size_t kCapacity = 65536 - 2 * sizeof(uint32_t);

        uint32_t    size = 0;               ///< Length of valid data
        uint32_t    used = 0;               ///< Number of bytes consumed (from start of data)
        std::byte   data[kCapacity];        ///< The data itself

        size_t available() const {return size - used;}
        bool empty() const       {return size == used;}

        ConstBuf read(size_t maxLen) {
            size_t n = std::min(maxLen, available());
            ConstBuf result(data + used, n);
            used += n;
            return result;
        }

        void unRead(size_t len) {
            assert(len <= used);
            used -= len;
        }
    };

    using BufferRef = std::unique_ptr<Buffer>;


    /** Wrapper around a uv_stream_t. Used by Pipe and TCPSocket. */
    struct uv_stream_wrapper {
        explicit uv_stream_wrapper(uv_stream_t *stream) :_stream(stream) {_stream->data = this;}
        explicit uv_stream_wrapper(uv_pipe_t *pipe)     :uv_stream_wrapper((uv_stream_t*)pipe) { }

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

}
