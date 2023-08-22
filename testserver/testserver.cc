//
//  main.cpp
//  testserver
//
//  Created by Jens Alfke on 8/18/23.
//

#include "AsyncUV.hh"
#include "Task.hh"
#include <iostream>

using namespace std;
using namespace snej::coro;

#define CRLF  "\r\n"


static Task connectionTask(std::shared_ptr<uv::TCPSocket> client) {
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


int main(int argc, const char * argv[]) {
    uv::TCPServer server(34567);
    return uv::UVMain(argc, argv, [&]{
        cout << "Listening on port 34567\n";
        server.listen([](std::shared_ptr<uv::TCPSocket> client) {
            connectionTask(std::move(client));
        });
    });
}
