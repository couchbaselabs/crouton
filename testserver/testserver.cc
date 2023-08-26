//
// testserver.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "Crouton.hh"
#include "Task.hh"
#include <iostream>

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
    AWAIT client->shutdown();

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
