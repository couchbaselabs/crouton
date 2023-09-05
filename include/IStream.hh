//
// IStream.hh
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
#include <memory>
#include <span>
#include <string>
#include <string_view>

struct uv_buf_t;

namespace crouton {

    /** Low-level struct pointing to mutable data. Usually serves as the destination of a read. */
    class MutableBuf : public std::span<std::byte> {
    public:
        using span::span;
        using span::operator=;
        MutableBuf(std::string& str) :span((std::byte*)str.data(), str.size()) { }
        MutableBuf(uv_buf_t);

        template <typename T>
        MutableBuf(T* begin, size_t n)  :span((std::byte*)begin, n * sizeof(T)) { }
        template <>
        MutableBuf(void* begin, size_t n) :span((std::byte*)begin, n) { }

        explicit operator std::string_view() const {
            return std::string_view((const char*)data(), size());
        }
        explicit operator uv_buf_t() const;
    };

    
    /** Low-level struct pointing to immutable data. Usually serves as the source of a write. */
    class ConstBuf : public std::span<const std::byte> {
    public:
        using span::span;
        using span::operator=;
        ConstBuf(std::string_view str) :span((const std::byte*)str.data(), str.size()) { }
        ConstBuf(uv_buf_t);

        template <typename T>
        ConstBuf(const T* begin, size_t n)  :span((const std::byte*)begin, n * sizeof(T)) { }
        template <>
        ConstBuf(const void* begin, size_t n) :span((const std::byte*)begin, n) { }

        explicit operator std::string_view() const {
            return std::string_view((const char*)data(), size());
        }
        explicit operator uv_buf_t() const;
    };


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
        [[nodiscard]] virtual Future<ConstBuf> readNoCopy(size_t maxLen = 65536);

        /// Makes the last `len` read bytes unread again.
        /// The last read call must have been `readNoCopy`.
        /// `len` may not be greater than the number of bytes returned by `readNoCopy`.
        virtual void unRead(size_t len);

        /// Reads `len` bytes, copying into memory starting at `dst` (which must remain valid.)
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] virtual Future<size_t> read(MutableBuf buf);

        /// Reads `len` bytes, returning them as a string.
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<std::string> readString(size_t maxLen);

        /// Reads exactly `len` bytes; on eof, throws UVError(UV_EOF).
        [[nodiscard]] Future<void> readExactly(MutableBuf);

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

        /// Writes data, fully. The string is copied, so the caller doesn't need to keep it.
        [[nodiscard]] Future<void> write(std::string);

        /// Writes data, fully, from multiple input buffers.
        /// @warning The data pointed to by the buffers must remain valid until completion.
        [[nodiscard]] virtual Future<void> write(const ConstBuf buffers[], size_t nBuffers);
        [[nodiscard]] Future<void> write(std::initializer_list<ConstBuf> buffers);

    protected:
        /// The abstract read method that subclasses must implement.
        [[nodiscard]] virtual Future<ConstBuf> _readNoCopy(size_t maxLen) =0;

        /// Abstract write method subclasses must implement.
        /// @note  If a subclass natively supports multi-buffer write ("writev"),
        ///     it can override the virtual multi-buffer write method too, and implement
        ///     this one to simply call it with one buffer.
        [[nodiscard]] virtual Future<void> _write(ConstBuf) =0;

    private:
        [[nodiscard]] Future<ConstBuf> i_readNoCopy(size_t maxLen);
        Future<size_t> i_read(MutableBuf);

        ConstBuf _readBuf;
        size_t _readUsed = 0;
        bool _readBusy = false;
        bool _writeBusy = false;
    };

}
