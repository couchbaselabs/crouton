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
#include "Future.hh"
#include "UVBase.hh"
#include <string>

namespace crouton {
    struct Buffer;
    struct stream_wrapper;


    /** Abstract interface of an asynchronous bidirectional stream.
        It has concrete read/write methods, which are merely conveniences that call the
        abstract ones.
        Re-entrant reads or writes are not allowed: no read call may be issued until the
        previous one has completed, and equivalently for writes. */
    class IStream {
    public:
        virtual ~IStream() = default;

        /// True if the stream is open.
        virtual bool isOpen() const =0;

        /// Resolves once the stream has opened.
        [[nodiscard]] virtual Future<void> open() = 0;

        /// Closes the stream; resolves when it's closed.
        [[nodiscard]] virtual Future<void> close() = 0;

        /// Closes the write side, but not the read side. (Like a socket's `shutdown`.)
        [[nodiscard]] virtual Future<void> closeWrite() = 0;

        //---- Reading:

        /// Lowest level read method.  Reads at least 1 byte, except at EOF.
        /// Returned buffer belongs to the stream, and is valid until the next read or close call.
        [[nodiscard]] virtual Future<ConstBuf> readNoCopy(size_t maxLen);

        /// Makes the last `len` read bytes unread again.
        /// The last read call must have been `readNoCopy`.
        /// `len` may not be greater than the number of bytes returned by `readNoCopy`.
        virtual void unRead(size_t len);

        /// Reads `len` bytes, copying into memory starting at `dst` (which must remain valid.)
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] virtual Future<int64_t> read(size_t len, void* dst);
        [[nodiscard]] Future<int64_t> read(MutableBuf buf)            {return read(buf.len, buf.base);}

        /// Reads `len` bytes, returning them as a string.
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<std::string> readString(size_t maxLen);

        /// Reads exactly `len` bytes; on eof, throws UVError(UV_EOF).
        [[nodiscard]] Future<void> readExactly(size_t len, void* dst);
        [[nodiscard]] Future<void> readExactly(MutableBuf buf) {return readExactly(buf.len, buf.base);}

        /// Reads up through the first occurrence of the string `end`,
        /// or when `maxLen` bytes have been read, whichever comes first.
        /// Throws `UV_EOF` if it hits EOF first.
        [[nodiscard]] Future<std::string> readUntil(std::string end, size_t maxLen = SIZE_MAX);

        /// Reads until EOF.
        [[nodiscard]] Future<std::string> readAll() {return readString(SIZE_MAX);}

        //---- Writing:

        /// Writes the entire buffer.
        /// The buffer must remain valid until this call completes.
        [[nodiscard]] Future<void> write(ConstBuf);
        [[nodiscard]] Future<void> write(size_t len, const void *src) {return write(ConstBuf{src,len});}

        /// Writes data, fully. The string is copied, so the caller doesn't need to keep it.
        [[nodiscard]] Future<void> write(std::string);

        /// Writes data, fully, from multiple input buffers.
        /// @warning The data pointed to by the buffers must remain valid until completion.
        [[nodiscard]] virtual Future<void> write(const ConstBuf buffers[], size_t nBuffers);
        [[nodiscard]] Future<void> write(std::initializer_list<ConstBuf> buffers);

    protected:
        /// The abstract read method that subclasses must implement.
        [[nodiscard]] virtual Future<ConstBuf> _readNoCopy(size_t maxLen) =0;

        /// Marks the last `len` bytes from the last `_readNoCopy` call as unread.
        virtual void _unRead(size_t len) =0;

        /// Abstract write method subclasses must implement.
        /// @note  If a subclass natively supports multi-buffer write ("writev"),
        ///     it can override the virtual multi-buffer write method too, and implement
        ///     this one to simply call it with one buffer.
        [[nodiscard]] virtual Future<void> _write(ConstBuf) =0;

    private:
        Future<int64_t> _read(size_t len, void* dst);
        bool _readBusy = false;
        bool _writeBusy = false;
    };


    /** An asynchronous bidirectional stream. Abstract base class of Pipe and TCPSocket. */
    class Stream : public IStream {
    public:
        virtual ~Stream();

        /// Returns true while the stream is open.
        bool isOpen() const override {return _stream != nullptr;}

        /// Closes the write stream, leaving the read stream open until the peer closes it.
        [[nodiscard]] Future<void> closeWrite() override;

        /// Closes the stream entirely. (Called by the destructor.)
        virtual Future<void> close() override;

        //---- READING

        /// True if the stream has data available to read.
        bool isReadable() const;

        /// The number of bytes known to be available without blocking.
        size_t bytesAvailable() const;

        using IStream::read;

        //---- WRITING

        /// True if the stream has buffer space available to write to.
        bool isWritable() const;

        /// Writes as much as possible immediately, without blocking.
        /// @return  Number of bytes written, which may be 0 if the write buffer is full.
        size_t tryWrite(ConstBuf);

        using IStream::write;

    protected:
        Stream() = default;

        void opened(std::unique_ptr<stream_wrapper> s);

        std::unique_ptr<stream_wrapper> _stream;  // Handle for stream operations

    private:
        using BufferRef = std::unique_ptr<Buffer>;


        Stream(Stream const&) = delete;
        Stream& operator=(Stream const&) = delete;
        void _close();

        // IStream methods:
        [[nodiscard]] Future<ConstBuf> _readNoCopy(size_t maxLen) override;
        void _unRead(size_t len) override;
        [[nodiscard]] Future<void> _write(ConstBuf) override;
        [[nodiscard]] Future<void> write(const ConstBuf buffers[], size_t nBuffers) override;

        [[nodiscard]] Future<int64_t> _read(size_t len, void* dst);
        [[nodiscard]] Future<std::unique_ptr<Buffer>> readBuf();
        [[nodiscard]] Future<void> fillInputBuf();

        BufferRef _allocCallback(size_t);
        void _readCallback(BufferRef,int);

        std::vector<BufferRef> _input, _spare;
        std::optional<FutureProvider<BufferRef>> _futureBuf;
        int _readError = 0;
        std::unique_ptr<Buffer> _inputBuf;       // The last data read from the stream
        bool            _readBusy = false;  // Detects re-entrant calls
        bool            _writeBusy = false; // Detects re-entrant calls
    };

}
