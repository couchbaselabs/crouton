//
// Endian.hh
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
#include <bit>
#include <concepts>
#include <stdint.h>

namespace crouton::endian {

    static constexpr bool IsBig    = std::endian::native == std::endian::big;
    static constexpr bool IsLittle = std::endian::native == std::endian::little;
    static_assert(IsBig || IsLittle);

#ifdef __cpp_lib_byteswap
    // C++23 or later:
    template <typename T> using byteswap = std::byteswap;
#else
    #if defined(bswap16) || defined(bswap32) || defined(bswap64)
    #  error "unexpected define!" // freebsd may define these; probably just need to undefine them
    #endif

    /* Define byte-swap functions, using fast processor-native built-ins where possible */
    #if defined(_MSC_VER) // needs to be first because msvc doesn't short-circuit after failing defined(__has_builtin)
    #  define bswap16(x)     _byteswap_ushort((x))
    #  define bswap32(x)     _byteswap_ulong((x))
    #  define bswap64(x)     _byteswap_uint64((x))
    #elif (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
    #  define bswap16(x)     __builtin_bswap16((x))
    #  define bswap32(x)     __builtin_bswap32((x))
    #  define bswap64(x)     __builtin_bswap64((x))
    #elif defined(__has_builtin) && __has_builtin(__builtin_bswap64)  /* for clang; gcc 5 fails on this and && shortcircuit fails; must be after GCC check */
    #  define bswap16(x)     __builtin_bswap16((x))
    #  define bswap32(x)     __builtin_bswap32((x))
    #  define bswap64(x)     __builtin_bswap64((x))
    #else
        /* even in this case, compilers often optimize by using native instructions */
        Pure inline constexpr uint16_t bswap16(uint16_t x) {
            return ((( x  >> 8 ) & 0xffu ) | (( x  & 0xffu ) << 8 ));
        }
        Pure inline constexpr uint32_t bswap32(uint32_t x) {
            return ((( x & 0xff000000u ) >> 24 ) |
                    (( x & 0x00ff0000u ) >> 8  ) |
                    (( x & 0x0000ff00u ) << 8  ) |
                    (( x & 0x000000ffu ) << 24 ));
        }
        Pure inline constexpr uint64_t bswap64(uint64_t x) {
            return ((( x & 0xff00000000000000ull ) >> 56 ) |
                    (( x & 0x00ff000000000000ull ) >> 40 ) |
                    (( x & 0x0000ff0000000000ull ) >> 24 ) |
                    (( x & 0x000000ff00000000ull ) >> 8  ) |
                    (( x & 0x00000000ff000000ull ) << 8  ) |
                    (( x & 0x0000000000ff0000ull ) << 24 ) |
                    (( x & 0x000000000000ff00ull ) << 40 ) |
                    (( x & 0x00000000000000ffull ) << 56 ));
        }
    #endif
    template <std::integral T> Pure constexpr T byteswap(T) noexcept;
    template <> Pure constexpr int16_t  byteswap(int16_t i)  noexcept {return bswap16(i);}
    template <> Pure constexpr uint16_t byteswap(uint16_t i) noexcept {return bswap16(i);}
    template <> Pure constexpr int32_t  byteswap(int32_t i)  noexcept {return bswap32(i);}
    template <> Pure constexpr uint32_t byteswap(uint32_t i) noexcept {return bswap32(i);}
    template <> Pure constexpr int64_t  byteswap(int64_t i)  noexcept {return bswap64(i);}
    template <> Pure constexpr uint64_t byteswap(uint64_t i) noexcept {return bswap64(i);}
#endif

    template <std::integral T> Pure constexpr T encodeBig(T i) noexcept {
        if constexpr (IsBig) return i; else return byteswap<T>(i);
    }

    template <std::integral T> Pure constexpr T encodeLittle(T i) noexcept {
        if constexpr (IsLittle) return i; else return byteswap<T>(i);
    }

    template <std::integral T> Pure constexpr T decodeBig(T i) noexcept    {return encodeBig<T>(i);}
    template <std::integral T> Pure constexpr T decodeLittle(T i) noexcept {return encodeLittle<T>(i);}

}
