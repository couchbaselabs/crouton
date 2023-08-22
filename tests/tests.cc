//
//  tests.cc
//
//  Created by Jens Alfke on 8/16/23.
//

#include "AsyncUV.hh"
#include "Future.hh"
#include "Generator.hh"
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
}


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


static Future<int> futuristicSquare(int n) {
    AWAIT waitFor(500ms);
    RETURN n * n;
}


TEST_CASE("Waiter coroutine") {
    Generator<int> g1 = waiter("first");

    for (int i : g1)
        cerr << "--received " << i << endl;
}


TEST_CASE("Future coroutine") {
    auto f = futuristicSquare(12);
    int sqr = f.waitForValue();
    CHECK(sqr == 144);
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
}


TEST_CASE("DNS lookup", "[uv]") {
    AddrInfo addr;
    addr.lookup("mooseyard.com").waitForValue();
    cerr << "Addr = " << addr.primaryAddressString() << endl;
    auto ip4addr = addr.primaryAddress(4);
    REQUIRE(ip4addr);
    CHECK(ip4addr->sa_family == AF_INET);
    CHECK(addr.primaryAddressString() == "69.163.161.182");
}


static Future<string> readSocket() {
    TCPSocket socket;
    cerr << "Connecting...\n";
    AWAIT socket.connect("mooseyard.com", 80);

    cerr << "Writing...\n";
    AWAIT socket.write("GET / HTTP/1.1\r\nHost: mooseyard.com\r\nConnection: close\r\n\r\n");

    cerr << "Reading...\n";
    string result = AWAIT socket.readAll();
    RETURN result;
}


TEST_CASE("Read a socket", "[uv]") {
    Future<string> response = readSocket();
    string contents = response.waitForValue();
    cerr << "HTTP response:\n" << contents << endl;
    CHECK(contents.starts_with("HTTP/1.1 "));
    CHECK(contents.size() < 1000);
}
