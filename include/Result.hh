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
#include <stdexcept>
#include <cassert>
#include <exception>    
#include <utility>      
#include <variant>

namespace crouton {

    template <class X> concept Exceptional = std::derived_from<X, std::exception>;


    /** A Result holds either a value of type T, an exception, or nothing. */
    template <typename T>
    class Result {
    public:
        Result()                        :_value(std::exception_ptr{}) { }
        Result(T&& t)                   :_value(std::move(t)) { }
        Result(T const& t)              :_value(t) { }
        Result(std::exception_ptr x)    :_value(x) { }
        Result(Exceptional auto x)      :_value(std::make_exception_ptr(std::move(x))) { }

        /** Sets a value of type T, or a subclass of `std::exception`, or a `std::exception_ptr`.
            @note In `Result<void>`, to set a regular value call `set()` with no args. */
        void set(T&& t)                 {_value.template emplace<T>(std::move(t));}
        void set(T const& t)            {_value = t;}
        void set(nullptr_t)             {_value = T(nullptr);}
        void set(std::exception_ptr x)  {assert(x); _value = x;}
        void set(Exceptional auto x)    {_value= std::make_exception_ptr(std::move(x));}

        Result& operator=(Result const& r) = default;
        Result& operator=(Result&& r) = default;
        template <typename U>
            Result& operator=(U&& val)  {set(std::forward<U>(val)); return *this;}

        /// True if no value or exception has been set yet.
        bool empty() const {
            auto x = std::get_if<std::exception_ptr>(&_value);
            return x && *x == nullptr;
        }

        /// True if non-empty.
        explicit operator bool() const              {return !empty();}

        /// Returns the value, or throws the exception. Throws logic_error if empty.
        T const& value() const & {
            if (_value.index() == 1)
                return std::get_if<T>(&_value);
            else if (auto x = std::get<std::exception_ptr>(_value))
                std::rethrow_exception(x);
            else
                throw std::logic_error("Result has no value");
        }

        T&& value() && {
            if (_value.index() == 1)
                return std::get<T>(std::move(_value));
            else if (auto x = std::get<std::exception_ptr>(_value))
                std::rethrow_exception(x);
            else
                throw std::logic_error("Result has no value");
        }

        /// Returns the exception, or null if none.
        std::exception_ptr exception() const {
            auto x = std::get_if<std::exception_ptr>(&_value);
            return x ? *x : nullptr;
        }

    private:
        std::variant<std::exception_ptr,T> _value;
    };



    template <>
    class Result<void> {
    public:
        Result() = default;
        Result(std::exception_ptr x)    :_value(x) { }
        Result(Exceptional auto x)      :_value(std::make_exception_ptr(std::move(x))) { }

        void set()                      {_value = std::monostate{};}
        void set(std::exception_ptr x)  {assert(x); _value = x;}
        void set(Exceptional auto x)    {_value= std::make_exception_ptr(std::move(x));}

        Result& operator=(Result const& r) = default;
        Result& operator=(Result&& r) = default;
        template <typename U>
        Result& operator=(U&& val)      {set(std::forward<U>(val)); return *this;}

        bool empty() const {
            auto x = std::get_if<std::exception_ptr>(&_value);
            return x && *x == nullptr;
        }

        explicit operator bool() const              {return !empty();}

        void value() const {
            if (_value.index() == 0) {
                if (auto x = std::get<std::exception_ptr>(_value))
                    std::rethrow_exception(x);
                else
                    throw std::logic_error("Result has no value");
            }
        }

        std::exception_ptr exception() const {
            auto x = std::get_if<std::exception_ptr>(&_value);
            return x ? *x : nullptr;
        }

    private:
        std::variant<std::exception_ptr,std::monostate> _value;
    };




}
