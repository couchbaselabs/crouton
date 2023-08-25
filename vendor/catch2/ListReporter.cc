//
// ListReporter.cc
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

#include "catch_amalgamated.hpp"
#include <iostream>

class ListReporter : public Catch::StreamingReporterBase {
public:
    ListReporter(Catch::ReporterConfig&& _config):
    StreamingReporterBase(CATCH_MOVE(_config))
    {
    }

    static std::string getDescription() {
        return "Reporter that logs the beginning and end of a test";
    }

    void testCaseStarting(Catch::TestCaseInfo const& testInfo) override  {
        StreamingReporterBase::testCaseStarting(testInfo);
        std::cout << ">>>>>>>>>> " << testInfo.name << " >>>>>>>>>>\n";
    }

    void testCasePartialStarting(Catch::TestCaseInfo const& testInfo,
                                 uint64_t partNumber) override {
        if (partNumber > 1)
            std::cout << ">>>> Part " << partNumber << '\n';
    }

    void testCaseEnded(Catch::TestCaseStats const& testCaseStats) override {
        std::cout << "<<<<<<<<<< END " << currentTestCaseInfo->name << "\n\n";
        StreamingReporterBase::testCaseEnded(testCaseStats);
    }
};


CATCH_REGISTER_REPORTER("list", ListReporter)

/*
 Catch2 v3.4.0
 usage:
 Tests [<test name|pattern|tags> ... ] options

 where options are:
 -?, -h, --help                            display usage information
 -s, --success                             include successful tests in
 output
 -b, --break                               break into debugger on failure
 -e, --nothrow                             skip exception tests
 -i, --invisibles                          show invisibles (tabs, newlines)
 -o, --out <filename>                      default output filename
 -r, --reporter <name[::key=value]*>       reporter to use (defaults to
 console)
 -n, --name <name>                         suite name
 -a, --abort                               abort at first failure
 -x, --abortx <no. failures>               abort after x failures
 -w, --warn <warning name>                 enable warnings
 -d, --durations <yes|no>                  show test durations
 -D, --min-duration <seconds>              show test durations for tests
 taking at least the given number
 of seconds
 -f, --input-file <filename>               load test names to run from a
 file
 -#, --filenames-as-tags                   adds a tag for the filename
 -c, --section <section name>              specify section to run
 -v, --verbosity <quiet|normal|high>       set output verbosity
 --list-tests                              list all/matching test cases
 --list-tags                               list all/matching tags
 --list-reporters                          list all available reporters
 --list-listeners                          list all listeners
 --order <decl|lex|rand>                   test case order (defaults to
 decl)
 --rng-seed <'time'|'random-device'        set a specific seed for random
 |number>                                  numbers
 --colour-mode <ansi|win32|none            what color mode should be used as
 |default>                                 default
 --libidentify                             report name and version according
 to libidentify standard
 --wait-for-keypress <never|start|exit     waits for a keypress before
 |both>                                    exiting
 --skip-benchmarks                         disable running benchmarks
 --benchmark-samples <samples>             number of samples to collect
 (default: 100)
 --benchmark-resamples <resamples>         number of resamples for the
 bootstrap (default: 100000)
 --benchmark-confidence-interval           confidence interval for the
 <confidence interval>                     bootstrap (between 0 and 1,
 default: 0.95)
 --benchmark-no-analysis                   perform only measurements; do not
 perform any analysis
 --benchmark-warmup-time                   amount of time in milliseconds
 <benchmarkWarmupTime>                     spent on warming up each test
 (default: 100)
 --shard-count <shard count>               split the tests to execute into
 this many groups
 --shard-index <shard index>               index of the group of tests to
 execute (see --shard-count)
 --allow-running-no-tests                  Treat 'No tests run' as a success

 For more detailed usage please see the project docs
 */
