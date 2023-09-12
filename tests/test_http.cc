//
// test_http.cc
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
#include "HTTPParser.hh"

using namespace std;
using namespace crouton;


TEST_CASE("HTTP Request Parser", "[http]") {
    string_view req = "GET /foo/bar?x=y HTTP/1.1\r\n"
    "Foo: Bar\r\n"
    "Foo: Zab\r\n\r\n";

    HTTPParser parser(HTTPParser::Request);
    CHECK(parser.parseData(req));
    CHECK(parser.latestBodyData() == "");
    CHECK(parser.requestMethod == HTTPMethod::GET);
    CHECK(parser.requestURI.value().path == "/foo/bar");
    CHECK(parser.requestURI.value().query == "x=y");
    CHECK(parser.headers.size() == 1);
    CHECK(parser.headers["Foo"] == "Bar, Zab");
    CHECK(parser.complete());
}


TEST_CASE("HTTP Request Parser With Body", "[http]") {
    string_view req = "POST /foo/bar?x=y HTTP/1.1\r\n"
    "Content-Length: 20\r\n"
    "Foo: Bar\r\n"
    "Foo: Zab\r\n\r\n"
    "Here's the body";

    HTTPParser parser(HTTPParser::Request);
    CHECK(parser.parseData(req));
    CHECK(parser.latestBodyData() == "Here's the body");
    CHECK(parser.requestMethod == HTTPMethod::POST);
    CHECK(parser.requestURI.value().path == "/foo/bar");
    CHECK(parser.requestURI.value().query == "x=y");
    CHECK(parser.headers.size() == 2);
    CHECK(parser.headers["Foo"] == "Bar, Zab");
    CHECK(parser.headers["Content-Length"] == "20");
    CHECK(!parser.complete());

    string_view more = "54321";
    CHECK(parser.parseData(more));
    CHECK(parser.latestBodyData() == "54321");
    CHECK(parser.complete());
}


TEST_CASE("HTTP Response Parser", "[http]") {
    string_view req = "HTTP/1.1 200 Copacetic\r\n"
    "Content-Length: 20\r\n"
    "Foo: Bar\r\n"
    "Foo: Zab\r\n\r\n"
    "Here's the body";

    HTTPParser parser(HTTPParser::Response);
    CHECK(parser.parseData(req));
    CHECK(parser.latestBodyData() == "Here's the body");
    CHECK(parser.status == HTTPStatus::OK);
    CHECK(parser.statusMessage == "Copacetic");
    CHECK(parser.headers.size() == 2);
    CHECK(parser.headers["Foo"] == "Bar, Zab");
    CHECK(parser.headers["Content-Length"] == "20");
    CHECK(!parser.complete());

    string_view more = "54321HTTP/1.1 200 Copacetic";
    CHECK(parser.parseData(more));
    CHECK(parser.latestBodyData() == "54321");
    CHECK(parser.complete());
}


TEST_CASE("WebSocket Response Parser", "[http]") {
    string_view req = "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: HSmrc0sMlYUkAGmm5OPpG2HaGWk=\r\n"
    "Sec-WebSocket-Protocol: chat\r\n\r\n"
    "...websocketdatafromhereon...";

    HTTPParser parser(HTTPParser::Response);
    CHECK(parser.parseData(req));
    CHECK(parser.status == HTTPStatus::SwitchingProtocols);
    CHECK(parser.statusMessage == "Switching Protocols");
    CHECK(parser.headers.size() == 4);
    CHECK(parser.headers.get("Sec-WebSocket-Accept") == "HSmrc0sMlYUkAGmm5OPpG2HaGWk=");
    CHECK(parser.headers.get("Sec-WebSocket-Protocol") == "chat");
    CHECK(parser.complete());
    CHECK(parser.upgraded());
    CHECK(parser.latestBodyData() == "...websocketdatafromhereon...");
}


TEST_CASE("HTTP GET", "[uv][http]") {
    auto test = []() -> Future<void> {
        HTTPConnection connection("http://example.com/");
        HTTPRequest req{.uri = "/foo"};
        HTTPResponse resp = AWAIT connection.send(req);

        cout << "Status: " << int(resp.status()) << " " << resp.statusMessage() << endl;
        CHECK(resp.status() == HTTPStatus::NotFound);
        CHECK(resp.statusMessage() == "Not Found");
        cout << "Headers:\n";
        for (auto &h : resp.headers())
            cout << '\t' << h.first << " = " << h.second << endl;
        CHECK(resp.headers().size() >= 7);

        cout << "BODY:\n";
        string body = AWAIT resp.readAll();
        cout << body << endl;
        CHECK(body.starts_with("<!doctype html>"));
        CHECK(body.size() >= 200);
    };
    waitFor(test());
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTPS GET", "[uv][http]") {
    auto test = []() -> Future<void> {
        HTTPConnection connection("https://example.com/");
        HTTPRequest req;
        HTTPResponse resp = AWAIT connection.send(req);

        cout << "Status: " << int(resp.status()) << " " << resp.statusMessage() << endl;
        CHECK(resp.status() == HTTPStatus::OK);
        CHECK(resp.statusMessage() == "OK");
        cout << "Headers:\n";
        for (auto &h : resp.headers())
            cout << '\t' << h.first << " = " << h.second << endl;
        CHECK(resp.headers().size() >= 7);
        cout << "BODY:\n";
        string body = AWAIT resp.readAll();
        cout << body << endl;
        CHECK(body.starts_with("<!doctype html>"));
        CHECK(body.size() >= 1000);
    };
    waitFor(test());
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTPs GET Streaming", "[uv][http]") {
    auto test = []() -> Future<void> {
        HTTPConnection connection("https://mooseyard.com");
        HTTPRequest req{.uri = "/Music/Mine/Easter.mp3"};
        HTTPResponse resp = AWAIT connection.send(req);

        cout << "Status: " << int(resp.status()) << " " << resp.statusMessage() << endl;
        CHECK(resp.status() == HTTPStatus::OK);
        cout << "BODY:\n";
        size_t len = 0;
        while(true) {
            ConstBytes chunk = AWAIT resp.readNoCopy();
            cout << "\t...read " << chunk.size() << " bytes\n";
            if (chunk.size() == 0)
                break;
            len += chunk.size();
        }
        cout << "Total bytes read: " << len << endl;
        CHECK(len == 4086469);
    };
    waitFor(test());
    REQUIRE(Scheduler::current().assertEmpty());
}


