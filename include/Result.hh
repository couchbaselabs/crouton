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
        Result()                        :_value(noerror) { }
        Result(Error err)               :_value(err) { }

        template <typename U> requires std::constructible_from<T, U>
        Result(U&& val)                 :_value(std::forward<U>(val)) { }

        template <typename U> requires std::constructible_from<T, U>
        Result& operator=(U&& val)      {set(std::forward<U>(val)); return *this;}
        Result& operator=(Error err)    {_value = err; return *this;}

        Result(Result&& r) noexcept = default;
        Result(Result const& r) = default;
        Result& operator=(Result const& r) = default;
        Result& operator=(Result&& r) noexcept = default;

        template <typename U> requires std::constructible_from<T, U>
        void set(U&& val)               {_value = std::forward<U>(val);}

        /// True if there is a T value.
        bool ok() const                 {return _value.index() == 0;}

        /// True if there is neither a value nor an error.
        bool empty() const {
            Error const* err = std::get_if<Error>(&_value);
            return err && !*err;
        }

        /// True if there is an error.
        bool isError() const {
            Error const* err = std::get_if<Error>(&_value);
            return err && *err;
        }

        /// True if there's a value, false if empty. If there's an error, raises it.
        explicit operator bool() const {
            if (Error const* err = std::get_if<Error>(&_value)) {
                if (*err)
                    err->raise();
                return false;
            } else {
                return true;
            }
        }

        /// Returns the value, or throws the error as an exception.
        T const& value() const & {
            if (!ok())
                raise();
            return std::get<T>(_value);
        }

        T& value() & {
            if (!ok())
                raise();
            return std::get<T>(_value);
        }

        /// Returns the value, or throws the error as an exception.
        /// If the Result is empty, throws CroutonError::EmptyResult.
        T&& value() && {
            if (!ok())
                raise();
            return std::get<T>(std::move(_value));
        }

        T& operator*()              {return value();}
        T const& operator*() const  {return value();}

        T* operator->()             {return &value();}

        /// Returns the error, if any, else an empty Error.
        Error error() const {
            Error const* err = std::get_if<Error>(&_value);
            return err ? *err : Error{};
        }

        friend std::ostream& operator<<(std::ostream& out, Result const& r) {
            if (r.ok())
                return out << r.value();
            else
                return out << r.error();
        }

    private:
        [[noreturn]] void raise() const {
            Error err = std::get<Error>(_value);
            if (!err)
                err = CroutonError::EmptyResult;
            err.raise();
        }

        std::variant<T,Error> _value;
    };



    template <>
    class Result<void> {
    public:
        /// A default `Result<void>` has a (void) value and no error.
        Result()                                    :_value(noerror) { }
        Result(Error err)                           :_value(err) { }

        Result& operator=(Error err)    {_value = err; return *this;}

        Result(Result&& r) noexcept = default;
        Result(Result const& r) = default;
        Result& operator=(Result const& r) = default;
        Result& operator=(Result&& r) noexcept = default;

        void set()                                  {_value = std::monostate();}

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
