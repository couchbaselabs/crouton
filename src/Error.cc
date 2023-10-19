//
// Error.cc
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

#include "Error.hh"
#include "Backtrace.hh"
#include "Internal.hh"
#include "util/Logging.hh"
#include <iostream>
#include <mutex>
#include <sstream>

namespace crouton {
    using namespace std;


#pragma mark - ERROR


    string_view Error::domain() const {
        if (_code != 0)
            return sDomains[_domain].name;
        return "";
    }


    Error::DomainInfo Error::typeInfo() const {
#if CROUTON_RTTI
        if (_code != 0)
            return *(std::type_info*)sDomains[_domain].type;
        return typeid(void);
#else
        if (_code != 0)
            return sDomains[_domain].type;
        return nullptr;
#endif
    }


    string Error::description() const {
        if (_code != 0) {
            if (auto desc = sDomains[_domain].description) {
                if (string d = desc(_code); !d.empty())
                    return d;
            }
        }
        return brief();
    }


    string Error::brief() const {
        if (_code == 0)
            return "(no error)";
        return string(domain()) + " error " + to_string(_code);
    }

    std::ostream& operator<< (std::ostream& out, Error const& err) {
        return out << err.description();
    }


    void Error::raise(string_view logMessage, std::source_location const& loc) const {
        Log->error("*** Throwing crouton::Exception({}, {}): {} ({}) -- from {} [{}:{}]",
                      domain(), _code, description(), logMessage,
                      loc.function_name(), loc.file_name(), loc.line());
        fleece::Backtrace(1).writeTo(std::cerr);
        precondition(*this); // it's illegal to throw `noerror`
        throw Exception(*this);
    }


#pragma mark - ERROR DOMAINS:


    std::array<Error::DomainMeta,Error::kNDomains> Error::sDomains;


    uint8_t Error::_registerDomain(DomainInfo typeRef,
                                   string_view name,
                                   ErrorDescriptionFunc description)
    {
        // Note: This is only called once per domain, because it's called to initialize a static
        // variable in the private Error constructor; since this constructor is a template, there's
        // a separate static variable for each domain.
        static mutex sMutex;
        unique_lock<mutex> lock(sMutex);

        const void* type;
#if CROUTON_RTTI
        type = &typeRef;
#else
        type = typeRef;
#endif
        Log->debug("Error: Registering domain {}", name);
        for (auto &d : sDomains) {
            if (d.type == type) {
                return uint8_t(&d - &sDomains[0]);
            } else if (d.type == nullptr ) {
                d.type = type;
                d.name = name;
                d.description = description;
                return uint8_t(&d - &sDomains[0]);
            }
        }
        throw std::runtime_error("Too many ErrorDomains registered");
    }


    string NameEntry::lookup(int code, span<const NameEntry> table) {
        for (auto &entry : table) {
            if (entry.code == code)
                return entry.name;
        }
        return "";
    }


    string ErrorDomainInfo<CroutonError>::description(errorcode_t code) {
        using enum CroutonError;
        static constexpr NameEntry names[] = {
            {errorcode_t(Cancelled), "operation was cancelled"},
            {errorcode_t(EmptyResult), "internal error (accessed empty Result)"},
            {errorcode_t(InvalidArgument), "internal error (invalid argument)"},
            {errorcode_t(InvalidState), "internal error (invalid state)"},
            {errorcode_t(InvalidURL), "invalid URL"},
            {errorcode_t(LogicError), "internal error (logic error)"},
            {errorcode_t(ParseError), "unreadable data"},
            {errorcode_t(Timeout), "operation timed out"},
            {errorcode_t(EndOfData), "unexpected end of data"},
            {errorcode_t(Unimplemented), "unimplemented operation"},
        };
        return NameEntry::lookup(code, names);
    }


#pragma mark - EXCEPTIONS:


    using enum CppError;

    static constexpr NameEntry kExceptionNames[] = {
        {errorcode_t(exception), "std::exception"},
        {errorcode_t(logic_error), "std::logic_error"},
        {errorcode_t(invalid_argument), "std::invalid_argument"},
        {errorcode_t(domain_error), "std::domain_error"},
        {errorcode_t(length_error), "std::length_error"},
        {errorcode_t(out_of_range), "std::out_of_range"},
        {errorcode_t(runtime_error), "std::runtime_error"},
        {errorcode_t(range_error), "std::range_error"},
        {errorcode_t(overflow_error), "std::overflow_error"},
        {errorcode_t(underflow_error), "std::underflow_error"},
        {errorcode_t(regex_error), "std::regex_error"},
        {errorcode_t(system_error), "std::system_error"},
        {errorcode_t(format_error), "std::format_error"},
        {errorcode_t(bad_typeid), "std::bad_typeid"},
        {errorcode_t(bad_cast), "std::bad_cast"},
        {errorcode_t(bad_any_cast), "std::bad_any_cast"},
        {errorcode_t(bad_optional_access), "std::bad_optional_access"},
        {errorcode_t(bad_weak_ptr), "std::bad_weak_ptr"},
        {errorcode_t(bad_function_call), "std::bad_function_call"},
        {errorcode_t(bad_alloc), "std::bad_alloc"},
        {errorcode_t(bad_array_new_length), "std::bad_array_new_length"},
        {errorcode_t(bad_exception), "std::bad_exception"},
        {errorcode_t(bad_variant_access), "std::bad_variant_access"},
    };


    string ErrorDomainInfo<CppError>::description(errorcode_t code) {
        using enum CppError;
        return NameEntry::lookup(code, kExceptionNames);
    }


    static CppError exceptionToCppError(std::exception const& x) {
#if CROUTON_RTTI
        string name = fleece::Unmangle(typeid(x));
        for (auto &entry : kExceptionNames) {
            if (0 == strcmp(entry.name, name.c_str()))
                return CppError{entry.code};
        }
        Log->warn("No CppErr enum value matches exception class {}", name);
#endif
        return exception;
    }


    Error::Error(std::exception const& x) {
#if CROUTON_RTTI
        if (auto exc = dynamic_cast<Exception const*>(&x))
            *this = exc->error();
        else
#endif
            *this = exceptionToCppError(x);
    }


    Error::Error(std::exception_ptr xp) {
        if (xp) {
            try {
                std::rethrow_exception(xp);
            } catch (Exception const& x) {
                *this = x.error();
            } catch (std::exception &x) {
                *this = exceptionToCppError(x);
            } catch (...) {
                *this = CppError::exception;
            }
        }
    }


}
