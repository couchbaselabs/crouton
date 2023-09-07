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

    /** Low-level struct pointing to mutable data.
        Usually serves as the destination argument of a `read`. */
    class MutableBytes : public std::span<std::byte> {
    public:
        using span::span;       // inherits all of std::span's constructors
        using span::operator=;
        MutableBytes(std::string& str) :span((std::byte*)str.data(), str.size()) { }
        MutableBytes(uv_buf_t);

        template <typename T>
        MutableBytes(T* begin, size_t n)  :span((std::byte*)begin, n * sizeof(T)) { }
        MutableBytes(void* begin, size_t n) :span((std::byte*)begin, n) { }

        explicit operator std::string_view() const {
            return std::string_view((const char*)data(), size());
        }
        explicit operator uv_buf_t() const;
    };

    
    /** Low-level struct pointing to immutable data.
        Usually serves as the source of a `write`, or as a returned buffer from `readNoCopy`. */
    class ConstBytes : public std::span<const std::byte> {
    public:
        using span::span;       // inherits all of std::span's constructors
        using span::operator=;
        ConstBytes(std::string_view str) :span((const std::byte*)str.data(), str.size()) { }
        ConstBytes(uv_buf_t);

        template <typename T>
        ConstBytes(const T* begin, size_t n)  :span((const std::byte*)begin, n * sizeof(T)) { }
        ConstBytes(const void* begin, size_t n) :span((const std::byte*)begin, n) { }

        explicit operator std::string_view() const {
            return std::string_view((const char*)data(), size());
        }
        explicit operator uv_buf_t() const;
    };


    /** Abstract interface of an asynchronous bidirectional stream.
        It has concrete read/write methods, which are merely conveniences that call the
        abstract ones.
        @warning Re-entrant reads or writes are not allowed: no read call may be issued until the
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

        /// Reads at least 1 byte, except at EOF, and no more than `maxLen`.
        /// The bytes are read into an internal buffer that's returned to the caller.
        /// @warning The returned buffer belongs to the stream, and is only valid until the next
        ///          read or close call.
        /// @note This is an abstract method that subclasses must implement.
        [[nodiscard]] virtual Future<ConstBytes> readNoCopy(size_t maxLen = 65536) =0;

        /// Returns the next available unread bytes, always at least 1 byte except at EOF.
        /// Unlike `readNoCopy`, the returned bytes are _not consumed_ -- they will
        /// be returned again by the next read operation.
        /// To consume _n_ bytes after peeking, call `readNoCopy(n)`.
        /// @warning The returned buffer belongs to the stream, and is only valid until the next
        ///          read or close call.
        /// @note This is an abstract method that subclasses must implement.
        [[nodiscard]] virtual Future<ConstBytes> peekNoCopy() =0;

        /// Reads `len` bytes, copying into memory starting at `dst` (which must remain valid.)
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] virtual Future<size_t> read(MutableBytes buf);

        /// Reads `len` bytes, returning them as a string.
        /// Will always read the full number of bytes unless it hits EOF.
        ASYNC<std::string> readString(size_t maxLen);

        /// Reads exactly `len` bytes; on eof, throws UVError(UV_EOF).
        ASYNC<void> readExactly(MutableBytes);

        /// Reads up through the first occurrence of the string `end`,
        /// or when `maxLen` bytes have been read, whichever comes first.
        /// Throws `UV_EOF` if it hits EOF first.
        ASYNC<std::string> readUntil(std::string end, size_t maxLen = SIZE_MAX);

        /// Reads until EOF.
        ASYNC<std::string> readAll() {return readString(SIZE_MAX);}

        //---- Writing:

        /// Writes all the bytes.
        /// @warning The memory must remain valid until this call completes.
        /// @note This is the abstract write method that subclasses must implement.
        [[nodiscard]] virtual Future<void> write(ConstBytes) =0;

        /// Writes data, fully. The string is copied, so the caller doesn't need to keep it.
        ASYNC<void> write(std::string);

        /// Writes data, fully, from multiple input buffers.
        /// @warning The data pointed to by the buffers must remain valid until completion.
        /// @note  The default implementation makes `nBuffers` calls to `write(ConstBytes)`.
        ///        A subclass that natively supports multi-buffer write ("writev") may override
        ///        this method as an optimization.
        [[nodiscard]] virtual Future<void> write(const ConstBytes buffers[], size_t nBuffers);

        ASYNC<void> write(std::initializer_list<ConstBytes> buffers);
    };

}
