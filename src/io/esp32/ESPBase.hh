//
// ESPBase.hh
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

namespace crouton::io::esp {

    enum class ESPError : errorcode_t {
        none = 0,
        HostNotFound,
    };

    
    /** Same as LWIP's `err_t` */
    enum class LWIPError : errorcode_t {
        /** No error, everything OK. */
        ERR_OK         = 0,
        /** Out of memory error.     */
        ERR_MEM        = -1,
        /** Buffer error.            */
        ERR_BUF        = -2,
        /** Timeout.                 */
        ERR_TIMEOUT    = -3,
        /** Routing problem.         */
        ERR_RTE        = -4,
        /** Operation in progress    */
        ERR_INPROGRESS = -5,
        /** Illegal value.           */
        ERR_VAL        = -6,
        /** Operation would block.   */
        ERR_WOULDBLOCK = -7,
        /** Address in use.          */
        ERR_USE        = -8,
        /** Already connecting.      */
        ERR_ALREADY    = -9,
        /** Conn already established.*/
        ERR_ISCONN     = -10,
        /** Not connected.           */
        ERR_CONN       = -11,
        /** Low-level netif error    */
        ERR_IF         = -12,

        /** Connection aborted.      */
        ERR_ABRT       = -13,
        /** Connection reset.        */
        ERR_RST        = -14,
        /** Connection closed.       */
        ERR_CLSD       = -15,
        /** Illegal argument.        */
        ERR_ARG        = -16
    };

}

namespace crouton {
    template <> struct ErrorDomainInfo<io::esp::ESPError> {
        static constexpr string_view name = "ESP32";
        static string description(errorcode_t);
    };
    template <> struct ErrorDomainInfo<io::esp::LWIPError> {
        static constexpr string_view name = "LWIP";
        static string description(errorcode_t);
    };
}
