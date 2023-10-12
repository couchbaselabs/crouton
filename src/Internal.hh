//
// Internal.hh
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
#include "Error.hh"
#include <span>

namespace crouton {

    /** Utility for implementing name lookup, as in `ErrorDomainInfo<T>::description`.*/
    struct NameEntry {
        errorcode_t code;
        const char* name;

        /// Given a code, finds the first matching entry in the list and returns its name, else "".
        static string lookup(int code, std::span<const NameEntry>);
    };



    class NotReentrant {
    public:
        explicit NotReentrant(bool& scope)
        :_scope(scope)
        {
            if (_scope)
                Error::raise(CroutonError::LogicError, "Illegal reentrant call");
            _scope = true;
        }

        ~NotReentrant() {_scope = false;}

    private:
        bool& _scope;
    };

}
