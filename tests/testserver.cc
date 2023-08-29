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
#include "Task.hh"
#include <iostream>
#include <memory>

using namespace std;
using namespace crouton;

#define CRLF  "\r\n"


static Task connectionTask(std::shared_ptr<TCPSocket> client) {
    cout << "Accepted connection!\n";
    string request;
    request = AWAIT client->readUntil(CRLF CRLF);

    cout << "Request: " << request << endl;
    AWAIT client->write("HTTP/1.1 200 OK" CRLF
                        "Content-Type: text/plain; charset=utf-8" CRLF CRLF
                        "Hello, world!" CRLF);

    cout << "Sent response.\n";
    AWAIT client->closeWrite();

    cout << "Shutdown stream.\n";
    client->close();
    cout << "Done!\n\n";
}


static Task run() {
    static TCPServer server(34567);
    cout << "Listening on port 34567\n";
    server.listen([](std::shared_ptr<TCPSocket> client) {
        connectionTask(std::move(client));
    });
    RETURN;
}


int main(int argc, const char * argv[]) {
    return UVMain(argc, argv, run);
}
