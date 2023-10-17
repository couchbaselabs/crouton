//
// ESPBase.cc
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

#include "ESPBase.hh"
#include "Misc.hh"
#include <esp_random.h>
#include <lwip/err.h>

namespace crouton {
    using namespace std;
    using namespace crouton::io::esp;

    void Randomize(void* buf, size_t len) {
        esp_fill_random(buf, len);
    }

    string ErrorDomainInfo<ESPError>::description(errorcode_t code) {
        switch (ESPError(code)) {
            case ESPError::HostNotFound:  return "Host not found";
            default: break;
        }
        return "???";
    };

    string ErrorDomainInfo<LWIPError>::description(errorcode_t code) {
        return string(lwip_strerr(code));
    };

}
