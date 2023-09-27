//
// test_generator.cc
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
#include "Select.hh"


// An example Generator of successive integers.
static Generator<int64_t> counter(int64_t start, int64_t limit, bool slow = false) {
    for (int64_t i = start; i <= limit; i++) {
        YIELD i;
        if (slow)
            AWAIT Timer::sleep(0.05);
    }
}


// An example Generator of Fibonacci numbers.
static Generator<int64_t> fibonacci(int64_t limit, bool slow = false) {
    int64_t a = 1, b = 1;
    YIELD a;
    while (b <= limit) {
        YIELD b;
        tie(a, b) = pair{b, a + b};
        if (slow)
            AWAIT Timer::sleep(0.05);
    }
}


// A filter that passes only even numbers. Also takes int64 and produces int
static Generator<int64_t> onlyEven(Generator<int64_t> source) {
    // In a coroutine, you co_await a Generator instead of calling next():
    Result<int64_t> value;
    while ((value = AWAIT source)) {
        if (*value % 2 == 0)
            YIELD *value;
    }
}


// Converts int to string
static Generator<string> toString(Generator <int64_t> source) {
    while (Result<int64_t> value = AWAIT source) {
        YIELD to_string(*value) + "i";
    }
}


TEST_CASE("Generator", "[generator]") {
    RunCoroutine([]() -> Future<void> {
        Generator<int64_t> fib = fibonacci(100);
        vector<int64_t> results;
        Result<int64_t> n;
        while ((n = AWAIT fib)) {
            cerr << n << ' ';
            results.push_back(n.value());
        }
        cerr << endl;
        CHECK(results == vector<int64_t>{ 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89 });
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Generator without coroutine", "[generator]") {
    {
        Generator<int64_t> fib = fibonacci(100);
        vector<int64_t> results;
        for (Result<int64_t> n : fib) {
            cerr << n << ' ';
            results.push_back(n.value());
        }
        cerr << endl;
        CHECK(results == vector<int64_t>{ 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89 });
    }
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Generators", "[generator]") {
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



TEST_CASE("Select Generators", "[generator]") {
    RunCoroutine([]() -> Future<void> {
        Generator count = counter(-100, -80, true);
        Generator fib = fibonacci(100, true);

        int64_t expectedCount = -100;
        int64_t expectedFib = 1, expectedNextFib = 1;

        Select select { &count, &fib };

        bool countDone = false, fibDone = false;
        while(!countDone || !fibDone) {
            if (!countDone)
                select.enable(0);
            if (!fibDone)
                select.enable(1);
            Result<int64_t> result;
            switch (int which = AWAIT select) {
                case 0:
                    result = AWAIT count;
                    if (result) {
                        CHECK(result.value() == expectedCount);
                        ++expectedCount;
                    } else {
                        countDone = true;
                    }
                    break;
                case 1:
                    result = AWAIT fib;
                    if (result) {
                        CHECK(result.value() == expectedFib);
                        auto nextnext = expectedFib + expectedNextFib;
                        expectedFib = expectedNextFib;
                        expectedNextFib = nextnext;
                    } else {
                        fibDone = true;
                    }
                    break;
                default:
                    INFO(which);
                    FAIL("Unexpected result from Select");
            }
            cerr << result << ", ";
        }
        CHECK(expectedCount == -79);
        CHECK(expectedFib == 144);
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Select Future and Generator", "[generator]") {
    RunCoroutine([]() -> Future<void> {
        Generator count = counter(1, 1000, true);
        Future<void> timeout = Timer::sleep(1.0);
        Select select { &count, &timeout };
        select.enable(0);
        select.enable(1);
        int64_t expectedCount = 1;
        bool done = false;
        while (!done) {
            switch (int which = AWAIT select) {
                case 0: {
                    Result<int64_t> r = AWAIT count;
                    cerr << r << ", ";
                    CHECK(*r == expectedCount);
                    CHECK(expectedCount <= 22);
                    ++expectedCount;
                    select.enable(0);
                    break;
                }
                case 1: {
                    //AWAIT timeout;    // unnecessary
                    done = true;
                }
            }
        }
        CHECK(expectedCount >= 18);
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Generators in parallel queue", "[generator]") {
    RunCoroutine([]() -> Future<void> {
        BoundedAsyncQueue<int64_t> q(1);
        Task t1 = q.pushGenerator(counter(-100, -90, true));
        Task t2 = q.pushGenerator(fibonacci(100, true));

        Generator<int64_t> gen = q.generate();

        int64_t expectedCount = -100;
        int64_t expectedFib = 1, expectedNextFib = 1;
        for (Result<int64_t> r; (r = AWAIT gen); ) {
            int64_t n = r.value();
            cerr << n << ", ";
            if (n == expectedCount)
                ++expectedCount;
            else if (n == expectedFib) {
                auto nextnext = expectedFib + expectedNextFib;
                expectedFib = expectedNextFib;
                expectedNextFib = nextnext;
            } else {
                FAIL("Unexpected number " << n);
            }
        }
        cerr << endl;

        cerr << "Waiting for tasks to stop...\n";
        t1.interrupt();
        t2.interrupt();
        Scheduler::current().runUntil([&] {return !t1.alive() && !t2.alive();});
        
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


