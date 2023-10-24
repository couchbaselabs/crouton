//
// UVBase.hh
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
#include "Error.hh"

namespace crouton::io::uv {

    /** Enum for using libuv errors with Error. */
    enum class UVError : errorcode_t { };

}

namespace crouton {
    template <> struct ErrorDomainInfo<io::uv::UVError> {
        static constexpr string_view name = "libuv";
        static string description(errorcode_t);
    };
}
