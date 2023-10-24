//
// demo_server.cc
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
#include "io/TCPServer.hh"
#include "util/Logging.hh"
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>

using namespace std;
using namespace crouton;
using namespace crouton::io;

/* NOTE to newbies: this file uses some simple/optional macros that are used everywhere in Crouton
   to highlight suspend points and use of asynchronous code:

    staticASYNC     --> [[nodiscard]] static Future
    AWAIT           --> co_await
    RETURN          --> co_return
*/

static constexpr uint16_t kPort = 34567;


staticASYNC<void> serveRoot(http::Handler::Request const& req, http::Handler::Response& res) {
    res.writeHeader("Content-Type", "text/plain");
    AWAIT res.writeToBody("Hi!\r\n");
    RETURN noerror;
}


staticASYNC<void> serveWebSocket(http::Handler::Request const& req, http::Handler::Response& res) {
    ws::ServerWebSocket socket;
    if (! AWAIT socket.connect(req, res))
        RETURN noerror;

    Log->info("-- Opened WebSocket");
    Generator<ws::Message> rcvr = socket.receive();
    Result<ws::Message> msg;
    while ((msg = AWAIT rcvr)) {
        Log->info("\treceived {}", minifmt::write(msg));
        switch (msg->type) {
            case ws::Message::Text:
            case ws::Message::Binary:
                (void) socket.send(*msg); // no need to wait
                break;
            case ws::Message::Close:
                AWAIT socket.send(*msg); // echo the close request to complete the close.
                break;
            default:
                break;              // WebSocket itself handles Ping and Pong
        }
    }
    Log->info("-- Closing WebSocket");
    AWAIT socket.close();
    RETURN noerror;
}


static vector<http::Handler::Route> sRoutes = {
    {http::Method::GET, regex("/"),     serveRoot},
    {http::Method::GET, regex("/ws/?"), serveWebSocket},
};


static Task connectionTask(std::shared_ptr<TCPSocket> client) {
    Log->info("-- Accepted connection");
    http::Handler handler(client, sRoutes);
    AWAIT handler.run();
    Log->info("-- Done!\n");
}


static Task run() {
    static TCPServer server(kPort);
    Log->info("Listening at http://localhost:{}/ and ws://localhost:{}/ws", kPort, kPort);
    server.listen([](std::shared_ptr<TCPSocket> client) {
        connectionTask(std::move(client));
    });
    RETURN;
}


CROUTON_MAIN(run)
