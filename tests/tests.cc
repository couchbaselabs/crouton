//
//  tests.cc
//
//  Created by Jens Alfke on 8/16/23.
//

#include "AsyncUV.hh"
#include "Future.hh"
#include "Generator.hh"
#include "URL.hh"
#include <iostream>
#include <sys/socket.h>

#include "catch_amalgamated.hpp"

using namespace std;
using namespace snej::coro;
using namespace snej::coro::uv;


// An example Generator of Fibonacci numbers.
static Generator<int64_t> fibonacci(int64_t limit) {
    int64_t a = 1, b = 1;
    YIELD a;
    while (b <= limit) {
        YIELD b;
        tie(a, b) = pair{b, a + b};
    }
}


// A filter that passes only even numbers. Also takes int64 and produces int
static Generator<int64_t> onlyEven(Generator<int64_t> source) {
    // In a coroutine, you co_await a Generator instead of calling next():
    while (optional<int64_t> value = AWAIT source) {
        if (*value % 2 == 0)
            YIELD *value;
    }
}


// Converts int to string
static Generator<string> toString(Generator <int64_t> source) {
    while (optional<int> value = AWAIT source) {
        YIELD to_string(*value) + "i";
    }
}


TEST_CASE("Generator coroutine", "[coroutines]") {
    cerr << "Creating Generator...\n";
    Generator fib = toString( onlyEven( fibonacci(100000) ) );

    cerr << "Calling Generator...\n";

    vector<string> results;
    int n = 0;
    for (string const& value : fib) {
        results.push_back(value);
        cerr << "got " << (value) << endl;
        if (++n >= 100) {
            cerr << "...OK, that's enough output!\n";
            break;
        }
    }

    cerr << "Done!\n";
    CHECK(results == vector<string>{ "2i", "8i", "34i", "144i", "610i", "2584i", "10946i", "46368i" });
    REQUIRE(Scheduler::current().assertEmpty());
}


#if 0
static Future<void> waitFor(chrono::milliseconds ms) {
    FutureProvider<void> f;
    Timer::after(ms.count() / 1000.0, [f] {
        cerr << "\tTimer fired\n";
        f.setValue();
    });
    return f;
}


static Generator<int> waiter(string name) {
    for (int i = 1; i < 4; i++) {
        cerr << "waiter " << name << " generating " << i << endl;
        YIELD i;
        cerr << "waiter " << name << " waiting..." << endl;

        AWAIT waitFor(500ms);
    }
    cerr << "waiter " << name << " done" << endl;
}


TEST_CASE("Waiter coroutine") {
    Generator<int> g1 = waiter("first");

    for (int i : g1)
        cerr << "--received " << i << endl;
    REQUIRE(Scheduler::current().assertEmpty());
}


static Future<int> futuristicSquare(int n) {
    AWAIT waitFor(500ms);
    RETURN n * n;
}

TEST_CASE("Future coroutine") {
    auto f = futuristicSquare(12);
    int sqr = f.waitForValue();
    CHECK(sqr == 144);
    REQUIRE(Scheduler::current().assertEmpty());
}
#endif

TEST_CASE("URLs", "[uv]") {
    {
        URL url("http://mooseyard.com:8080/~jens?foo=bar");
        CHECK(url.scheme == "http");
        CHECK(url.hostname == "mooseyard.com");
        CHECK(url.port == 8080);
        CHECK(url.path == "/~jens");
        CHECK(url.query == "foo=bar");
    }
    {
        URL url("http://mooseyard.com");
        CHECK(url.scheme == "http");
        CHECK(url.hostname == "mooseyard.com");
        CHECK(url.port == 0);
        CHECK(url.path == "");
    }
}


static Future<string> readFile(string const& path) {
    string contents;
    FileStream f;
    AWAIT f.open(path);
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
    Future<string> cf = readFile("/Users/snej/Brewfile");
    string contents = cf.waitForValue();
    cerr << "File contents: \n--------\n" << contents << "\n--------"<< endl;
    CHECK(contents.size() == 715);
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("DNS lookup", "[uv]") {
    AddrInfo addr;
    addr.lookup("mooseyard.com").waitForValue();
    cerr << "Addr = " << addr.primaryAddressString() << endl;
    auto ip4addr = addr.primaryAddress(4);
    REQUIRE(ip4addr);
    CHECK(ip4addr->sa_family == AF_INET);
    CHECK(addr.primaryAddressString() == "69.163.161.182");
    REQUIRE(Scheduler::current().assertEmpty());
}


static Future<string> readSocket(const char* hostname, bool tls) {
    TCPSocket socket;
    cerr << "Connecting...\n";
    AWAIT socket.connect(hostname, (tls ? 443 : 80), tls);

    cerr << "Writing...\n";
    AWAIT socket.write("GET / HTTP/1.1\r\nHost: mooseyard.com\r\nConnection: close\r\n\r\n");

    cerr << "Reading...\n";
    string result = AWAIT socket.readAll();
    RETURN result;
}


TEST_CASE("Read a socket", "[uv]") {
    Future<string> response = readSocket("mooseyard.com", false);
    string contents = response.waitForValue();
    cerr << "HTTP response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.size() < 1000); // it will be a brief 301 response
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Read a TLS socket", "[uv]") {
    Future<string> response = readSocket("mooseyard.com", true);
    string contents = response.waitForValue();
    cerr << "HTTPS response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.ends_with("</HTML>\n"));
    CHECK(contents.size() == 2010); // the full 200 response
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTP GET", "[uv]") {
    auto test = []() -> Future<void> {
        HTTPClient client("http://mooseyard.com");
        HTTPRequest req(client, "GET", "/");
        HTTPResponse resp = AWAIT req.response();
        cout << "Status: " << int(resp.status) << " " << resp.statusMessage << endl;
        CHECK(resp.status == HTTPStatus::MovedPermanently);
        CHECK(resp.statusMessage == "Moved Permanently");
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
        CHECK(body.starts_with("<!DOCTYPE HTML"));
        CHECK(body.size() >= 200);
    };
    test().waitForValue();
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("HTTPS GET", "[uv]") {
    auto test = []() -> Future<void> {
        HTTPClient client("https://mooseyard.com");
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
        CHECK(body.starts_with("<HTML>"));
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
