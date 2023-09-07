//
// Stream.hh
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
#include "IStream.hh"
#include "UVBase.hh"
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct uv_stream_s;

namespace crouton {
    struct Buffer;

    /** An asynchronous bidirectional stream. Abstract base class of Pipe and TCPSocket. */
    class Stream : public IStream {
    public:
        virtual ~Stream();

        /// Returns true while the stream is open.
        bool isOpen() const override {return _stream != nullptr;}

        /// Closes the write stream, leaving the read stream open until the peer closes it.
        ASYNC<void> closeWrite() override;

        /// Closes the stream entirely. (Called by the destructor.)
        virtual Future<void> close() override;

        //---- READING

        /// True if the stream has data available to read.
        bool isReadable() const;

        /// The number of bytes known to be available without blocking.
        size_t bytesAvailable() const;

        ASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        ASYNC<ConstBytes> peekNoCopy() override;

        using IStream::read;

        //---- WRITING

        /// True if the stream has buffer space available to write to.
        bool isWritable() const;

        /// Writes as much as possible immediately, without blocking.
        /// @return  Number of bytes written, which may be 0 if the write buffer is full.
        size_t tryWrite(ConstBytes);

        using IStream::write;
        ASYNC<void> write(ConstBytes) override;
        ASYNC<void> write(const ConstBytes buffers[], size_t nBuffers) override;

    protected:
        Stream();

        void opened(uv_stream_s*);

    private:
        using BufferRef = std::unique_ptr<Buffer>;

        Stream(Stream const&) = delete;
        Stream& operator=(Stream const&) = delete;
        void _close();

        ASYNC<int64_t> _read(size_t len, void* dst);
        ASYNC<std::unique_ptr<Buffer>> readBuf();
        ASYNC<ConstBytes> fillInputBuf();

        void read_start();
        void _allocCallback(size_t suggested, uv_buf_t*);
        void _readCallback(ssize_t nread, const uv_buf_t*);

        uv_stream_s*            _stream = nullptr;  // The libuv stream
        std::vector<BufferRef>  _input;             // Buffers of already-read data
        std::vector<BufferRef>  _spare;             // Recycled buffers waiting to be reused
        BufferRef               _readingBuf;        // Buffer currently being filled by libuv
        std::optional<FutureProvider<BufferRef>> _futureBuf;  // Client data request
        int                     _readError = 0;     // Error read from stream
        std::unique_ptr<Buffer> _inputBuf;          // The last data read from the stream
        bool                    _reading = false;   // True after uv_read_start
        bool                    _readBusy = false;  // Detects re-entrant calls
    };

}
