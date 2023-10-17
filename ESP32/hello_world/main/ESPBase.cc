//
// ESPBase.cc
//
// 
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

#include "ESPBase.hh"
#include <lwip/err.h>

namespace crouton {
    using namespace std;

    string ErrorDomainInfo<esp::ESPError>::description(errorcode_t code) {
        switch (esp::ESPError(code)) {
            case esp::ESPError::HostNotFound:  return "Host not found";
            default: break;
        }
        return "???";
    };

    string ErrorDomainInfo<esp::LWIPError>::description(errorcode_t code) {
        return string(lwip_strerr(code));
    };

}
