//
//  tests.cc
//
//  Created by Jens Alfke on 8/16/23.
//

#include "Generator.hh"
#include "Future.hh"
#include "AsyncIO.hh"
#include <iostream>
#include <future>
#include <sys/socket.h>

#include "catch_amalgamated.hpp"

using namespace std;
using namespace snej::coro;
using namespace snej::coro::uv;


// An example Generator of Fibonacci numbers.
static Generator<int64_t> fibonacci(int64_t limit) {
    int64_t a = 1, b = 1;
    co_yield a;
    while (b <= limit) {
        co_yield b;
        tie(a, b) = pair{b, a + b};
    }
}


// A filter that passes only even numbers. Also takes int64 and produces int
static Generator<int64_t> onlyEven(Generator<int64_t> source) {
    // In a coroutine, you co_await a Generator instead of calling next():
    while (optional<int64_t> value = co_await source) {
        if (*value % 2 == 0)
            co_yield *value;
    }
}


// Converts int to string
static Generator<string> toString(Generator <int64_t> source) {
    while (optional<int> value = co_await source) {
        co_yield to_string(*value) + "i";
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


static Future<bool> waitFor(chrono::milliseconds ms) {
    FutureProvider<bool> f;
    thread thrd([ms,f] {
        cerr << "\tthread running...\n";
        this_thread::sleep_for(ms);
        cerr << "\tthread calling resume...\n";
        f.setValue(true);
    });
    thrd.detach();
    return f;
}


static Generator<int> waiter(string name) {
    for (int i = 1; i < 4; i++) {
        cerr << "waiter " << name << " generating " << i << endl;
        co_yield i;
        cerr << "waiter " << name << " waiting..." << endl;

        co_await waitFor(500ms);
    }
    cerr << "waiter " << name << " done" << endl;
}


static Future<int> futuristicSquare(int n) {
    co_await waitFor(500ms);
    co_return n * n;
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
    bool ok = co_await f.open(path);
    if (!ok) {
        cerr << "Couldn't open file " << path << endl;
        co_return contents;
    }
    char buffer[100];
    while (true) {
        int64_t len = co_await f.read(sizeof(buffer), &buffer[0]);
        if (len < 0)
            cerr << "File read error " << len << endl;
        if (len <= 0)
            break;
        cerr << "Read " << len << "bytes" << endl;
        contents.append(buffer, len);
    }
    cerr << "Returning contents\n";
    co_return contents;
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
    co_await socket.connect("mooseyard.com", 80);

    cerr << "Writing...\n";
    co_await socket.write("GET / HTTP/1.1\r\nHost: mooseyard.com\r\nConnection: close\r\n\r\n");

    cerr << "Reading...\n";
    string result;
    while (optional<string> input = co_await socket.reader()) {
        cerr << "\t...read " << input->size() << " bytes\n";
        result += *input;
    }
    co_return result;
}


TEST_CASE("Read a socket", "[uv]") {
    Future<string> response = readSocket();
    string contents = response.waitForValue();
    cerr << "HTTP response:\n" << contents << endl;
}
