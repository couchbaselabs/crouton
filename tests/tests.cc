//
// tests.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "tests.hh"

void RunCoroutine(Future<void> (*test)()) {
    Future<void> result = test();
    Scheduler::current().runUntil([&]{return result.hasValue();});
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


class TestActor : public Actor {
public:
    Future<int64_t> fibonacciSum(int n) const {
        std::cerr << "---begin fibonacciSum(" << n << ")\n";
        int64_t sum = 0;
        auto fib = fibonacci(INT_MAX);
        for (int i = 0; i < n; i++) {
            AWAIT Timer::sleep(0.1);//TEMP
            if (auto f = AWAIT fib)
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
        TestActor actor;
        cerr << "actor = " << (void*)&actor << endl;
        Future<int64_t> sum10 = actor.fibonacciSum(10);
        Future<int64_t> sum20 = actor.fibonacciSum(20);
        cerr << "Sum10 is " << (AWAIT sum10) << endl;
        cerr << "Sum20 is " << (AWAIT sum20) << endl;
        RETURN;
    });
}
