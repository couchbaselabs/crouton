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
#include <iomanip>
#include <iostream>
#include <memory>

using namespace std;
using namespace crouton;


static constexpr uint16_t kPort = 34567;


static Future<void> serveRoot(HTTPHandler::Request const& req, HTTPHandler::Response& res) {
    res.writeHeader("Content-Type", "text/plain");
    AWAIT res.writeToBody("Hi!\r\n");
}


static Future<void> serveWebSocket(HTTPHandler::Request const& req, HTTPHandler::Response& res) {
    ServerWebSocket ws;
    if (! AWAIT ws.connect(req, res))
        RETURN;

    cerr << "-- Opened WebSocket\n";
    while (!ws.readyToClose()) {
        WebSocket::Message msg = AWAIT ws.receive();
        cerr << "\treceived " << msg << endl;
        switch (msg.type) {
            case WebSocket::Text:
            case WebSocket::Binary:
                (void) ws.send(msg); // no need to wait
                break;
            case WebSocket::Close:
                AWAIT ws.send(msg); // echo the close request to complete the close.
                break;
            default:
                break;              // WebSocket itself handles Ping and Pong
        }
    }
    cerr << "-- Closing WebSocket\n";
    ws.close();
}


static vector<HTTPHandler::Route> sRoutes = {
    {HTTPMethod::GET, regex("/"),     serveRoot},
    {HTTPMethod::GET, regex("/ws/?"), serveWebSocket},
};


static Task connectionTask(std::shared_ptr<TCPSocket> client) {
    cout << "-- Accepted connection\n";
    HTTPHandler handler(client, sRoutes);
    AWAIT handler.run();
    cout << "-- Done!\n\n";
}


static Task run() {
    static TCPServer server(kPort);
    cout << "Listening at http://localhost:" << kPort << "/ and ws://localhost:" << kPort << "/ws\n";
    server.listen([](std::shared_ptr<TCPSocket> client) {
        connectionTask(std::move(client));
    });
    RETURN;
}


CROUTON_MAIN(run)
