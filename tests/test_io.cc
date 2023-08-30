//
// test_io.cc
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

#include "tests.hh"
#include "NWConnection.hh"
#include <sys/socket.h> // for AF_INET

using namespace std;
using namespace crouton;


TEST_CASE("URLs", "[uv]") {
    {
        URL url("http://example.com:8080/~jens?foo=bar");
        CHECK(url.scheme == "http");
        CHECK(url.hostname == "example.com");
        CHECK(url.port == 8080);
        CHECK(url.path == "/~jens");
        CHECK(url.query == "foo=bar");
    }
    {
        URL url("http://example.com");
        CHECK(url.scheme == "http");
        CHECK(url.hostname == "example.com");
        CHECK(url.port == 0);
        CHECK(url.path == "");
    }
}


static Future<string> readFile(string const& path) {
    string contents;
    FileStream f(path);
    AWAIT f.open();
    char buffer[100];
    while (true) {
        int64_t len = AWAIT f.read(sizeof(buffer), &buffer[0]);
        if (len < 0)
            cerr << "File read error " << len << endl;
        if (len <= 0)
            break;
        cerr << "Read " << len << "bytes" << endl;
        contents.append(buffer, len);
    }
    cerr << "Returning contents\n";
    RETURN contents;
}


TEST_CASE("Read a file", "[uv]") {
    Future<string> cf = readFile("README.md");
    string contents = cf.waitForValue();
    //cerr << "File contents: \n--------\n" << contents << "\n--------"<< endl;
    CHECK(contents.size() > 500);
    CHECK(contents.size() < 5000);
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("DNS lookup", "[uv]") {
    AddrInfo addr = AddrInfo::lookup("example.com").waitForValue();
    cerr << "Addr = " << addr.primaryAddressString() << endl;
    auto ip4addr = addr.primaryAddress(4);
    CHECK(ip4addr.sa_family == AF_INET);
    CHECK(addr.primaryAddressString() == "93.184.216.34");
    REQUIRE(Scheduler::current().assertEmpty());
}


static Future<string> readSocket(const char* hostname, bool tls) {
    TCPSocket socket;
    cerr << "Connecting...\n";
    AWAIT socket.connect(hostname, (tls ? 443 : 80), tls);

    cerr << "Writing...\n";
    AWAIT socket.write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

    cerr << "Reading...\n";
    string result = AWAIT socket.readAll();
    RETURN result;
}


TEST_CASE("Read a socket", "[uv]") {
    Future<string> response = readSocket("example.com", false);
    string contents = response.waitForValue();
    cerr << "HTTP response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.size() > 1000);
    CHECK(contents.size() < 2000);
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Read a TLS socket", "[uv]") {
    Future<string> response = readSocket("example.com", true);
    string contents = response.waitForValue();
    cerr << "HTTPS response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.ends_with("</html>\n"));
    CHECK(contents.size() > 1000);
    CHECK(contents.size() < 2000);
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTP GET", "[uv]") {
    auto test = []() -> Future<void> {
        HTTPClient client("http://example.com/foo");
        HTTPRequest req(client, "GET", "/");
        HTTPResponse resp = AWAIT req.response();
        cout << "Status: " << int(resp.status) << " " << resp.statusMessage << endl;
        CHECK(resp.status == HTTPStatus::NotFound);
        CHECK(resp.statusMessage == "Not Found");
        cout << "Headers:\n";
        auto headers = resp.headers();
        int n = 0;
        while (auto header = AWAIT headers) {
            cout << '\t' << header->first << " = " << header->second << endl;
            ++n;
        }
        CHECK(n >= 7);
        cout << "BODY:\n";
        string body = AWAIT resp.entireBody();
        cout << body << endl;
        CHECK(body.starts_with("<!doctype html>"));
        CHECK(body.size() >= 200);
    };
    test().waitForValue();
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTPS GET", "[uv]") {
    auto test = []() -> Future<void> {
        HTTPClient client("https://example.com");
        HTTPRequest req(client, "GET", "/");
        HTTPResponse resp = AWAIT req.response();
        cout << "Status: " << int(resp.status) << " " << resp.statusMessage << endl;
        CHECK(resp.status == HTTPStatus::OK);
        CHECK(resp.statusMessage == "OK");
        cout << "Headers:\n";
        auto headers = resp.headers();
        int n = 0;
        while (auto header = AWAIT headers) {
            cout << '\t' << header->first << " = " << header->second << endl;
            ++n;
        }
        CHECK(n >= 10);
        cout << "BODY:\n";
        string body = AWAIT resp.entireBody();
        cout << body << endl;
        CHECK(body.starts_with("<!doctype html>"));
        CHECK(body.size() >= 1000);
    };
    test().waitForValue();
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTPs GET Streaming", "[uv]") {
    auto test = []() -> Future<void> {
        HTTPClient client("https://mooseyard.com");
        HTTPRequest req(client, "GET", "/Music/Mine/Easter.mp3");
        HTTPResponse resp = AWAIT req.response();
        cout << "Status: " << int(resp.status) << " " << resp.statusMessage << endl;
        CHECK(resp.status == HTTPStatus::OK);
        cout << "BODY:\n";
        size_t len = 0;
        while(true) {
            string chunk = AWAIT resp.readBody();
            cout << "\t...read " << chunk.size() << " bytes\n";
            len += chunk.size();
            if (chunk.empty())
                break;
        }
        cout << "Total bytes read: " << len << endl;
        CHECK(len == 4086469);
    };
    test().waitForValue();
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("WebSocket", "[uv]") {
    auto test = []() -> Future<void> {
        //FIXME: This requires Sync Gateway to be running locally
        WebSocket ws("ws://work.local:4985/travel-sample/_blipsync");
        ws.setHeader("Sec-WebSocket-Protocol", "BLIP_3+CBMobile_3");
        HTTPStatus status = AWAIT ws.connect();
        REQUIRE(status == HTTPStatus::Connected);
        AWAIT ws.send("foo");
        ws.close();
    };
    test().waitForValue();
    REQUIRE(Scheduler::current().assertEmpty());
}


#ifdef __APPLE__

static Future<string> readNWSocket(const char* hostname, bool tls) {
    NWConnection socket;
    cerr << "Connecting...\n";
    AWAIT socket.connect(hostname, (tls ? 443 : 80), tls);

    cerr << "Writing...\n";
    AWAIT socket.write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

    cerr << "Reading...\n";
    string result = AWAIT socket.readAll();
    RETURN result;
}


TEST_CASE("NWConnection", "[nw]") {
    Future<string> response = readNWSocket("example.com", false);
    string contents = response.waitForValue();
    cerr << "HTTP response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.size() > 1000);
    CHECK(contents.size() < 2000);
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("NWConnection TLS", "[nw]") {
    Future<string> response = readNWSocket("example.com", true);
    string contents = response.waitForValue();
    cerr << "HTTP response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.size() > 1000);
    CHECK(contents.size() < 2000);
    REQUIRE(Scheduler::current().assertEmpty());
}

#endif // __APPLE__
