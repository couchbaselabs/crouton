//
// Result.hh
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
#include "Error.hh"
#include <cassert>
#include <utility>
#include <variant>

namespace crouton {

    /** A `Result<T>` holds either a value of type T or an Error.
        It's used as a return value, and as the contents of a `Future<T>`. */
    template <typename T>
    class Result {
    public:
        template <typename U> requires std::constructible_from<T, U>
        Result(U&& val)                 :_value(std::forward<U>(val)) { }

        Result(Error err)               :_value(err) { }

        template <typename U> requires std::constructible_from<T, U>
        Result& operator=(U&& val)      {_value = std::forward<U>(val); return *this;}
        Result& operator=(Error err)    {_value = err; return *this;}

        Result(Result&& r) noexcept = default;
        Result(Result const& r) = default;
        Result& operator=(Result const& r) = default;
        Result& operator=(Result&& r) noexcept = default;

        /// True if there is a T value.
        bool ok() const                             {return _value.index() == 0;}

        /// True if there is an error.
        bool isError() const                        {return _value.index() != 0;}

        /// Returns the value, or throws the error as an exception.
        T const& value() const & {
            if (!ok())
                raise();
            return std::get<T>(_value);
        }

        /// Returns the value, or throws the error as an exception.
        T&& value() && {
            if (!ok())
                raise();
            return std::get<T>(std::move(_value));
        }

        /// Returns the error, if any, else an empty Error.
        Error error() const {
            Error const* err = std::get_if<Error>(&_value);
            return err ? *err : Error{};
        }

    private:
        [[noreturn]] void raise() const       {std::get<Error>(_value).raise();}

        std::variant<T,Error> _value;
    };



    template <>
    class Result<void> {
    public:
        /// A default `Result<void>` has a (void) value and no error.
        Result() = default;
        Result(Error err)                           :_value(err) { }
        bool ok() const                             {return _value.index() == 0;}
        bool isError() const                        {return _value.index() != 0;}

        /// There's no value to return, but this checks for an error and if so throws it.
        void value() const {
            if (_value.index() != 0)
                std::get<Error>(_value).raise();
        }

        Error error() const {
            Error const* err = std::get_if<Error>(&_value);
            return err ? *err : Error{};
        }

    private:
        std::variant<std::monostate,Error> _value;
    };



    /// Syntactic sugar to handle a `Result<T>`, similar to Swift's `try`.
    /// - If R has a value, evaluates to its value.
    /// - If R has an error, `co_return`s the error from the current coroutine.
    #define UNWRAP(R) \
        ({ auto _r_ = (R); \
           if (_r_.isError())  co_return _r_.error(); \
           std::move(_r_).value(); })
    //FIXME: MSVC doesn't support `({...})`, AFAIK


    /// Syntactic sugar to await a Future without throwing exceptions.
    /// - If the future returns a value, evaluates to that value.
    /// - If the future returns an error, `co_return`s the error.
    #define TRY_AWAIT(F)    UNWRAP(AWAIT NoThrow(F))
}
