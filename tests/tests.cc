//
// tests.cc
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

void RunCoroutine(Future<void> (*test)()) {
    Future<void> f = test();
    Scheduler::current().runUntil([&]{return f.hasResult();});
    f.result(); // check exception
}


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
    optional<int64_t> value;
    while ((value = AWAIT source)) {
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
    InitLogging(); //FIXME: Put this somewhere where it gets run before any test
    {
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
    REQUIRE(Scheduler::current().assertEmpty());
}


#if 0
static Future<void> waitFor(chrono::milliseconds ms) {
    FutureProvider<void> f;
    Timer::after(ms.count() / 1000.0, [f] {
        cerr << "\tTimer fired\n";
        f.setResult();
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
    {
        Generator<int> g1 = waiter("first");

        for (int i : g1)
            cerr << "--received " << i << endl;
    }
    REQUIRE(Scheduler::current().assertEmpty());
}


static Future<int> futuristicSquare(int n) {
    AWAIT waitFor(500ms);
    RETURN n * n;
}

TEST_CASE("Future coroutine") {
    {
        auto f = futuristicSquare(12);
        int sqr = f.waitForValue();
        CHECK(sqr == 144);
    }
    REQUIRE(Scheduler::current().assertEmpty());
}
#endif


#ifdef __clang__    // GCC 12 doesn't seem to support ActorImpl ctor taking parameters

class TestActor : public Actor {
public:
    Future<int64_t> fibonacciSum(int n) const {
        std::cerr << "---begin fibonacciSum(" << n << ")\n";
        int64_t sum = 0;
        auto fib = fibonacci(INT_MAX);
        for (int i = 0; i < n; i++) {
            AWAIT Timer::sleep(0.1);
            optional<int64_t> f;
            if ((f = AWAIT fib))
                sum += f.value();
            else
                break;
        }
        std::cerr << "---end fibonacciSum(" << n << ") returning " << sum << "\n";
        RETURN sum;
    }
};


TEST_CASE("Actor") {
    RunCoroutine([]() -> Future<void> {
        auto actor = std::make_shared<TestActor>();
        cerr << "actor = " << (void*)actor.get() << endl;
        Future<int64_t> sum10 = actor->fibonacciSum(10);
        Future<int64_t> sum20 = actor->fibonacciSum(20);
        cerr << "Sum10 is " << (AWAIT sum10) << endl;
        cerr << "Sum20 is " << (AWAIT sum20) << endl;
        RETURN;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}

#endif // __clang__


TEST_CASE("Randomize") {
    uint8_t buf[10];
    ::memset(buf, 0, sizeof(buf));
    for (int pass = 0; pass < 5; ++pass) {
        Randomize(buf, sizeof(buf));
        for (int i = 0; i < 10; ++i)
            printf("%02x ", buf[i]);
        printf("\n");
    }
}
