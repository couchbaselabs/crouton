//
// test_pubsub.cc
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
#include "PubSub.hh"
#include "StringUtils.hh"
#include "io/FileStream.hh"

using namespace std;
using namespace crouton;
using namespace crouton::ps;


TEST_CASE("Emitter and Collector", "[pubsub]") {
    RunCoroutine([]() -> Future<void> {
        auto emit = make_shared<Emitter<string>>(initializer_list<string>{"hello", "world", "...", "goodbye"});
        Collector<string> collect(emit);
        collect.start();

        Scheduler::current().runUntil( [&] {return collect.done(); });
        CHECK(collect.items() == vector<string>{"hello", "world", "...", "goodbye" });
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Transformer", "[pubsub]") {
    RunCoroutine([]() -> Future<void> {
        auto emit = make_shared<Emitter<int>>(initializer_list<int>{1, 2, 4, 8, 16, 32});

        auto xform = make_shared<Transformer<int,string>>([](Result<int> in) -> Result<string> {
            if (in.ok())
                return std::to_string(in.value());
            else
                return in.error();
        });

        Collector<string> collect;
        emit | xform | collect;
        collect.start();

        Scheduler::current().runUntil( [&] {return collect.done(); });
        CHECK(collect.items() == vector<string>{"1", "2", "4", "8", "16", "32"});
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Filter", "[pubsub]") {
    RunCoroutine([]() -> Future<void> {
        auto collect = Emitter<int>(initializer_list<int>{1, 2, 3, 4, 5, 6})
                     | Filter<int>([](int i) {return i % 2 == 0;})
                     | Collector<int>{};
        collect.start();

        Scheduler::current().runUntil( [&] {return collect.done(); });

        CHECK(collect.items() == vector<int>{2, 4, 6});
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("BaseConnector", "[pubsub]") {
    static constexpr size_t kNSubscribers = 3;
    RunCoroutine([]() -> Future<void> {
        auto emit = make_shared<Emitter<int>>(initializer_list<int>{1, 2, 3, 4, 5, 6});
        auto connect = make_shared<BaseConnector<int>>(emit);

        vector<Collector<int>> colls(kNSubscribers);
        for (auto &coll : colls) {
            coll.subscribeTo(emit);
            coll.start();
        }

        Scheduler::current().runUntil( [&] {
            for (auto &coll : colls)
                if (!coll.done())
                    return false;
            return true;
        });

        int i = 0;
        for (auto &coll : colls) {
            INFO("colls[" << i++ << "]");
            CHECK(coll.items() == vector<int>{1, 2, 3, 4, 5, 6});
        }
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


/// Takes strings and splits them into lines at `\n`.
class LineSplitter : public BaseConnector<string> {
public:
    using super = BaseConnector<string>;
    using super::BaseConnector;
protected:
    ASYNC<void> produce(Result<string> input) override {
        if (!input.ok()) {
            AWAIT super::produce(std::move(input));
            RETURN noerror;
        }
        string_view rest(*input);
        while (!rest.empty()) {
            string_view line;
            tie(line, rest) = split(rest, '\n');
            AWAIT super::produce(string(line));
        }
        RETURN noerror;
    }
};

/// A string filter that passes strings containing a given substring.
class Contains : public Filter<string> {
public:
    Contains(string substring)                  :_substring(std::move(substring)) { }
protected:
    bool filter(string const& item) override    {return item.find(_substring) != string::npos;}
private:
    string _substring;
};


TEST_CASE("Stream Publisher", "[pubsub][io]") {
    // Let's read a text file and collect the lines containing the word "Crouton":
    auto test = []() -> Future<void> {
        auto collect = AnyPublisher<string,io::FileStream>("README.md")
                     | LineSplitter{}
                     | Contains("Crouton")
                     | Collector<string>{};

        collect.start();
        Scheduler::current().runUntil( [&] {return collect.done(); });

        vector<string> const& items = collect.items();
        for (size_t i = 0; i < items.size(); ++i) {
            cout << i << ": " << items[i] << endl;
            CHECK(items[i].find("Crouton") != string::npos);
        }
        CHECK(items.size() >= 7);
        RETURN noerror;
    };
    waitFor(test());
    REQUIRE(Scheduler::current().assertEmpty());
}
