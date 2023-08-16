//
//  tests.cc
//
//  Created by Jens Alfke on 8/16/23.
//

#include "Generator.hh"
#include "Future.hh"
#include <iostream>
#include <future>

#include "catch_amalgamated.hpp"

using namespace std;
using namespace tendril::coro;


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
    cout << "Creating Generator...\n";
    Generator fib = toString( onlyEven( fibonacci(100000) ) );

    cout << "Calling Generator...\n";

    vector<string> results;
    int n = 0;
    for (string const& value : fib) {
        results.push_back(value);
        cout << "got " << (value) << endl;
        if (++n >= 100) {
            cout << "...OK, that's enough output!\n";
            break;
        }
    }

    cout << "Done!\n";
    CHECK(results == vector<string>{ "2i", "8i", "34i", "144i", "610i", "2584i", "10946i", "46368i" });
}


static Future<bool> waitFor(chrono::milliseconds ms) {
    FutureProvider<bool> f;
    thread thrd([ms,f] {
        cout << "\tthread running...\n";
        this_thread::sleep_for(ms);
        cout << "\tthread calling resume...\n";
        f.setValue(true);
    });
    thrd.detach();
    return f;
}


static Generator<int> waiter(string name) {
    for (int i = 1; i < 4; i++) {
        cout << "waiter " << name << " generating " << i << endl;
        co_yield i;
        cout << "waiter " << name << " waiting..." << endl;

        co_await waitFor(500ms);
    }
    cout << "waiter " << name << " done" << endl;
}


static Future<int> futuristicSquare(int n) {
    co_await waitFor(500ms);
    co_return n * n;
}


TEST_CASE("Waiter coroutine", "[coroutines]") {
    Generator<int> g1 = waiter("first");

    for (int i : g1)
        cout << "--received " << i << endl;
}


TEST_CASE("Future coroutine", "[coroutines]") {
    auto f = futuristicSquare(12);
    int sqr = f.waitForValue();
    CHECK(sqr == 144);
}
