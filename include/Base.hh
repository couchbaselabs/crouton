//
// Base.hh
//
// 
//

#pragma once
#include <exception>
#include <iosfwd>
#include <memory>
#include <utility>
#include <cassert>

// Find <coroutine> in std or experimental.
// Always use `CORO_NS` instead of `std` for coroutine types
#if defined(__has_include)
#   if __has_include(<coroutine>)
#      include <coroutine>
#      define CORO_NS std
#   elif __has_include(<experimental/coroutine>)
#      include <experimental/coroutine>
#      define CORO_NS std::experimental
#   else
#      error No coroutine header
#   endif
#else
#   include <coroutine>
#   define CORO_NS std
#endif


// Synonyms for coroutine primitives. Optional, but they're more visible in the code.
#define AWAIT  co_await
#define YIELD  co_yield
#define RETURN co_return


namespace crouton {

    // `is_type_complete_v<T>` evaluates to true iff T is a complete (fully-defined) type.
    // By Raymond Chen: https://devblogs.microsoft.com/oldnewthing/20190710-00/?p=102678
    template<typename, typename = void>
    constexpr bool is_type_complete_v = false;
    template<typename T>
    constexpr bool is_type_complete_v<T, std::void_t<decltype(sizeof(T))>> = true;


    // `NonReference` is a concept that applies to any value that's not a reference.
    template <typename T>
    concept NonReference = !std::is_reference_v<T>;

}
