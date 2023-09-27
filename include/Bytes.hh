//
// Bytes.hh
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
#include "Base.hh"

#include <algorithm>
#include <cstring>
#include <span>

struct uv_buf_t;

namespace crouton {

    using std::byte;


    template <typename T, class Self>
    class Bytes : public std::span<T> {
    public:
        static_assert(sizeof(T) == 1);
        using super = std::span<T>;

        using super::span;       // inherits all of std::span's constructors
        using super::operator=;

        Bytes(string& str) :super((T*)str.data(), str.size()) { }

        explicit operator string_view() const {return {(const char*)this->data(), this->size()};}

        Self first(size_t n) const          {return Self(super::first(n));}
        Self last(size_t n) const           {return Self(super::last(n));}
        Self without_first(size_t n) const  {return last(this->size() - n);}
        Self without_last(size_t n) const   {return first(this->size() - n);}

        T* endByte() const                  {return this->data() + this->size();}
    };


    /** Low-level struct pointing to immutable data.
     Usually serves as the source of a `write`, or as a returned buffer from `readNoCopy`. */
    class ConstBytes : public Bytes<const byte, ConstBytes> {
    public:
        using Bytes::Bytes;
        using Bytes::operator=;

        explicit ConstBytes(std::span<element_type> s) :Bytes(s.data(), s.size()) { }
        ConstBytes(string_view str) :Bytes((element_type*)str.data(), str.size()) { }

        template <typename T>
        ConstBytes(const T* begin, size_t n)  :Bytes((element_type*)begin, n * sizeof(T)) { }
        ConstBytes(const void* begin, size_t n) :Bytes((element_type*)begin, n) { }

        template <typename T>
        ConstBytes(const T* begin, const T* end)  :ConstBytes(begin, end - begin) { }
        ConstBytes(const void* begin, const void* end) :ConstBytes(begin, uintptr_t(end) - uintptr_t(begin)) { }

        ConstBytes(uv_buf_t);
        explicit operator uv_buf_t() const;

        [[nodiscard]] size_t read(void *dstBuf, size_t dstSize) noexcept {
            size_t n = std::min(dstSize, size());
            ::memcpy(dstBuf, data(), n);
            *this = without_first(n);
            return n;
        }

        [[nodiscard]] ConstBytes read(size_t dstSize) noexcept {
            size_t n = std::min(dstSize, size());
            ConstBytes result = first(n);
            *this = without_first(n);
            return result;
        }

        [[nodiscard]] size_t readAll(void *dstBuf, size_t dstSize) noexcept {
            return dstSize <= size() ? read(dstBuf, dstSize) : 0;
        }
    };


    /** Low-level struct pointing to mutable data.
     Usually serves as the destination argument of a `read`. */
    class MutableBytes : public Bytes<byte, MutableBytes> {
    public:
        using Bytes::Bytes;
        using Bytes::operator=;

        explicit MutableBytes(std::span<element_type> s) :Bytes(s.data(), s.size()) { }

        template <typename U>
        MutableBytes(U* begin, size_t n)    :Bytes((byte*)begin, n * sizeof(U)) { }
        MutableBytes(void* begin, size_t n) :Bytes((byte*)begin, n) { }

        template <typename T>
        MutableBytes(T* begin, T* end)  :MutableBytes(begin, end - begin) { }
        MutableBytes(void* begin, void* end) :MutableBytes(begin, uintptr_t(end) - uintptr_t(begin)) { }

        MutableBytes(uv_buf_t);

        operator uv_buf_t() const;

        [[nodiscard]] size_t write(const void* src, size_t len) {
            size_t n = std::min(len, this->size());
            ::memcpy(data(), src, n);
            *this = without_first(n);
            return n;
        }

        [[nodiscard]] size_t write(ConstBytes bytes) {return write(bytes.data(), bytes.size());}
    };

}
