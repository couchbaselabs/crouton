//
// betterassert.hh
//
// Copyright © 2018 Couchbase. All rights reserved.
// Updated Sept 2022 by Jens Alfke to remove exception throwing
//

// This is an alternate implementation of `assert()` that produces a nicer message that includes
// the function name, and calls `std::terminate` instead of `abort`.
// NOTE: If <cassert> / <assert.h> is included later than this header, it will overwrite this
// definition of assert() with the usual one. (And vice versa.)

// `assert_always()`, `precondition()`, and `postcondition()` do basically the same thing:
// if the boolean parameter is false, they log a message (to stderr) and terminate the process.
// They differ only in the message logged.
//
// * `precondition()` should be used at the start of a function/method to test its parameters
//   or initial state. A failure should be interpreted as a bug in the method's _caller_.
// * `postcondition()` should be used at the end of a function/method to test its return value
//   or final state. A failure should be interpreted as a bug in the _method_.
// * `assert_always()` can be used in between to test intermediate state or results.
//   A failure may be a bug in the method, or in something it called.
//
// These are enabled in all builds regardless of the `NDEBUG` flag.

#ifndef assert_always
    #include <source_location>

    #ifndef __has_attribute
        #define __has_attribute(x) 0
    #endif
    #ifndef __has_builtin
        #define __has_builtin(x) 0
    #endif

    #if __has_attribute(noinline)
        #define NOINLINE        __attribute((noinline))
    #else
        #define NOINLINE
    #endif

    #define assert_always(e) \
        do {  if (!(e)) [[unlikely]] ::crouton::_assert_failed (#e);  } while (0)
    #define precondition(e) \
        do {  if (!(e)) [[unlikely]] ::crouton::_precondition_failed (#e);  } while (0)
    #define postcondition(e) \
        do {  if (!(e)) [[unlikely]] ::crouton::_postcondition_failed (#e);  } while (0)

    namespace crouton {
        [[noreturn]] NOINLINE void _assert_failed(const char *cond,
                                    std::source_location const& = std::source_location::current()) noexcept;
        [[noreturn]] NOINLINE void _precondition_failed(const char *cond,
                                    std::source_location const& = std::source_location::current()) noexcept;
        [[noreturn]] NOINLINE void _postcondition_failed(const char *cond,
                                    std::source_location const& = std::source_location::current()) noexcept;

        extern void (*assert_failed_hook)(const char *message);
    }
#endif // assert_always

// `assert()`, `assert_precondition()`, and `assert_postcondition()` are just like the macros
// above, except that they are disabled when `NDEBUG` is defined. They should be used when the
// evaluation of the expression would hurt performance in a release build.

#undef assert
#undef assert_precondition
#undef assert_postcondition
#ifdef NDEBUG
#   if __has_builtin(__builtin_assume)
#       define assert(e)           __builtin_assume(bool(e))
#   else
#       define assert(e)           (void(0))
#   endif
#   define assert_precondition(e)  assert(e)
#   define assert_postcondition(e) assert(e)
#else
#   define assert(e)               assert_always(e)
#   define assert_precondition(e)  precondition(e)
#   define assert_postcondition(e) postcondition(e)
#endif //NDEBUG
