//
// testserver.cc
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

#include "Crouton.hh"
#include "HTTPHandler.hh"
#include <functional>
#include <iostream>
#include <memory>

using namespace std;
using namespace crouton;


static vector<HTTPHandler::Route> sRoutes = {
    {
        HTTPMethod::GET,
        regex("/"),
        [](auto &request, auto &response) -> Future<void> {
            response.writeHeader("Content-Type", "text/plain");
            AWAIT response.writeToBody("Hi!\r\n");
        }
    }
};

static Task connectionTask(std::shared_ptr<TCPSocket> client) {
    cout << "-- Accepted connection\n";
    HTTPHandler handler(client, sRoutes);
    AWAIT handler.run();
    cout << "-- Done!\n\n";
    RETURN;
}


static Task run() {
    static TCPServer server(34567);
    cout << "Listening at http://localhost:34567/\n";
    server.listen([](std::shared_ptr<TCPSocket> client) {
        connectionTask(std::move(client));
    });
    RETURN;
}


CROUTON_MAIN(run)
