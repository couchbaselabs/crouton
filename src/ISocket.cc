//
// ISocket.cc
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

#include "ISocket.hh"
#include "TCPSocket.hh"
#include "TLSSocket.hh"

#ifdef __APPLE__
#include "NWConnection.hh"
#endif

namespace crouton {

    std::unique_ptr<ISocket> ISocket::newSocket(bool useTLS) {
#ifdef __APPLE__
        return std::make_unique<apple::NWConnection>(useTLS);
#else
        if (useTLS)
            return std::make_unique<mbed::TLSSocket>();
        else
            return std::make_unique<TCPSocket>();
#endif
    }


    Task ISocket::closeAndFree(std::unique_ptr<ISocket> sock) {
        AWAIT sock->close();
        // unique_ptr destructor will free ISocket
    }


}
