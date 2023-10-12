//
// Error.hh
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
#include "util/Base.hh"

#include <array>
#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

namespace crouton {

    /// Numeric base type of error codes. However, Error only uses 18 bits to store the code,
    /// giving a maximum range of ±131072. 
    /// @note I wanted to use `int16_t`, but some libraries use error codes outside that range,
    ///       notably Apple's Security framework.
    using errorcode_t = int32_t;


    /// Type of function that maps an error code to a human-readable description.
    using ErrorDescriptionFunc = string (*)(errorcode_t);


    /// A struct template that has to be specialized for each ErrorDomain enum.
    template <typename ERR> struct ErrorDomainInfo { };


    /// The concept `ErrorDomain` describes an `enum class` that can be used as a domain for Error.
    /// * The enum's base type must be `errorcode_t`.
    /// * The value 0 must not be used; it always represents the lack of an error.
    /// * the struct `crouton::ErrorDomainInfo<T>` must be specialized with static members:
    ///   * `name`, a `string_view` naming the error type
    ///   * `description`, an `ErrorDescriptionFunc` returning a description of an error code.
    template <typename T>
    concept ErrorDomain = requires {
        std::is_enum_v<T>;
        requires std::same_as<std::underlying_type_t<T>, errorcode_t>;
        {ErrorDomainInfo<T>::name} -> std::convertible_to<string_view>;
        {ErrorDomainInfo<T>::description} -> std::convertible_to<ErrorDescriptionFunc>;
    };



    /** Error holds an error code as a type-erased enum value, of any type that matches `ErrorDomain`.
        There's also a default empty state. */
    class Error {
    public:
        /// The default no-error value. Available as the constant `noerror`.
        constexpr Error() = default;

        /// Constructs an Error from an enum value.
        template <ErrorDomain D>
        Error(D d)                          :Error(errorcode_t(d), domainID<D>()) { }

        /// Constructs an Error from an enum value and a human-readable message.
        /// @note  The message is currently ignored, but may be preserved in the future.
        template <ErrorDomain D>
        Error(D d, string_view msg)         :Error(errorcode_t(d), domainID<D>()) { }

        /// Constructs an Error from a C++ exception object.
        explicit Error(std::exception const&);

        /// Constructs an Error from a caught C++ exception.
        explicit Error(std::exception_ptr);

        /// The error's code as a plain integer.
        errorcode_t code() const            {return _code;}

        /// The name of the error domain, which comes from `ErrorDomainInfo<T>::name`.
        string_view domain() const;

        /// The `type_info` metadata of the original enum type.
        std::type_info const& typeInfo() const;

        /// A human-readable description of the error.
        /// First calls `ErrorDomainInfo<D>::description` where `D` is the domain enum.
        /// If that function returns an empty string, defaults to the value of `brief`.
        string description() const;

        /// Returns the error's domain name and numeric code, or "(no error)" if none.
        string brief() const;

        /// Writes an error's description to a stream.
        friend std::ostream& operator<< (std::ostream&, Error const&);

        /// True if there is an error, false if none.
        explicit operator bool() const      {return _code != 0;}

        /// True if the error is of type D.
        template <ErrorDomain D> 
        bool is() const                     {return typeInfo() == typeid(D);}

        /// Converts the error code back into a D, if it is one.
        /// If its type isn't D, returns `D{0}`.
        template <ErrorDomain D>
        D as() const                        {return D{is<D>() ? _code : errorcode_t{0}};}

        /// Compares two Errors. Their domains and codes must match.
        friend bool operator== (Error const& a, Error const& b) = default;

        /// Compares an Error to an ErrorDomain enum value.
        friend bool operator== (Error const& err, ErrorDomain auto d) {
            return err.typeInfo() == typeid(d) && err.code() == errorcode_t(d);
        }

        /// Throws the error as an Exception.
        [[noreturn]] void raise(string_view logMessage = "") const;

        /// Throws the error as an Exception, if there is one.
        void raise_if(string_view logMessage = "") const      {if (*this) raise(logMessage);}

        /// Convenience that directly throws an Exception from an ErrorDomain enum.
        template <ErrorDomain D>
        [[noreturn]] static void raise(D d, string_view msg = "") {Error(d).raise(msg);}

    private:
        Error(errorcode_t code, uint8_t domain) :_code(code), _domain(domain) {assert(_code == code);}

        // NOTE: I'm keeping the size of Error down to 3 bytes so that Result<T>, which consists of
        // a std::variant one of whose types is Error, can be as small as 4 bytes.
        errorcode_t _code   :18 {0};    // The error code, or 0 if no error
        uint8_t     _domain : 6 {0};    // Index of the domain in sDomains

        //---- Static stuff for managing metadata about each ErrorDomain enum:

        static constexpr int kNDomains = (1<<6);

        template <ErrorDomain T>
        static uint8_t domainID() {
            static uint8_t id = _registerDomain(typeid(T),
                                                ErrorDomainInfo<T>::name,
                                                ErrorDomainInfo<T>::description);
            return id;
        }

        static uint8_t _registerDomain(std::type_info const&, string_view, ErrorDescriptionFunc);

        struct DomainMeta {
            std::type_info const*   type {nullptr};         // The C++ type_info of the enum
            string_view             name;                   // The name of the domain
            ErrorDescriptionFunc    description {nullptr};  // Function mapping codes to names
        };
        static std::array<DomainMeta,kNDomains> sDomains;   // Stores metadata about known domains
    };


    /// A constant denoting "no error", the empty Error value.
    constexpr Error noerror {};


    /** An Error wrapped in a C++ exception, for when it needs to be thrown. */
    class Exception : public std::runtime_error, public Error {
    public:
        Exception(Error err)        :runtime_error(err.description()), Error(err) { }
        Error error() const         {return *this;}
    };


    /// Crouton error codes.
    enum class CroutonError : errorcode_t {
        none = 0,
        Cancelled,                  // Operation was explicitly cancelled
        EmptyResult,                // Tried to get the value of an empty Result
        InvalidArgument,            // Caller passed an invalid argument value
        InvalidState,               // Callee is in an invalid state to perform this operation
        InvalidURL,                 // A URL is syntactically invalid
        LogicError,                 // Something impossible happened due to a bug
        ParseError,                 // Syntax error parsing something, like an HTTP stream.
        Timeout,                    // Operation failed because it took too long
        Unimplemented,              // Unimplemented functionality or abstract-by-convention method
    };

    template <> struct ErrorDomainInfo<CroutonError> {
        static constexpr string_view name = "Crouton";
        static string description(errorcode_t);
    };


    /// An ErrorDomain with codes for standard C++ exception classes.
    /// Intended for converting caught exceptions, not for using in your own Errors.
    enum class CppError : errorcode_t {
        exception = 1,
        logic_error,
            invalid_argument,
            domain_error,
            length_error,
            out_of_range,
        runtime_error,
            range_error,
            overflow_error,
            underflow_error,
            regex_error,
            system_error,
            format_error,
        bad_typeid,
        bad_cast,
            bad_any_cast,
        bad_optional_access,
        bad_weak_ptr,
        bad_function_call,
        bad_alloc,
            bad_array_new_length,
        bad_exception,
        bad_variant_access,
    };

    template <> struct ErrorDomainInfo<CppError> {
        static constexpr string_view name = "exception";
        static string description(errorcode_t);
    };

}
