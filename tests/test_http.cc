//
// test_http.cc
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
    CHECK(parser.headers["Sec-WebSocket-Accept"] == "HSmrc0sMlYUkAGmm5OPpG2HaGWk=");
    CHECK(parser.headers["Sec-WebSocket-Protocol"] == "chat");
    CHECK(parser.complete());
    CHECK(parser.upgraded());
    CHECK(parser.latestBodyData() == "...websocketdatafromhereon...");
}


