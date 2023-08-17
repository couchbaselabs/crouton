//
// UVBase.cc
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

#include "UVBase.hh"
#include "uv.h"

namespace snej::coro::uv {

    static const char* messageFor(int err) {
        switch (err) {
            case UV__EAI_NONAME:    return "unknown host";  // default msg is obscure/confusing
            default:                return uv_strerror(err);
        }
    }

    UVError::UVError(int status)
    :std::runtime_error(messageFor(status))
    ,err(status)
    { }

}
