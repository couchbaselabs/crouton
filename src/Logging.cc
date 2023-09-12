//
// Logging.cc
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

#include "Logging.hh"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <mutex>

namespace crouton {
    using namespace std;

    /// Defines the format of textual log output.
    static constexpr const char* kLogPattern = "▣ %H:%M:%S.%f %^%L | <%n>%$ %v";
    
    static vector<spdlog::sink_ptr> sSinks;
    static spdlog::sink_ptr         sStderrSink;

    std::shared_ptr<spdlog::logger> LCoro, LSched, LLoop, LNet;
    
    
    static shared_ptr<spdlog::logger> makeLogger(string_view name,
                                                 spdlog::level::level_enum level = spdlog::level::info)
    {
        string nameStr(name);
        auto logger = spdlog::get(nameStr);
        if (!logger) {
            logger = make_shared<spdlog::logger>(nameStr, sSinks.begin(), sSinks.end());
            spdlog::initialize_logger(logger);
            // set default level, unless already customized by SPDLOG_LEVEL:
            if (logger->level() == spdlog::level::info)
                logger->set_level(level);
        }
        return logger;
    }
    
    
    static void addSink(spdlog::sink_ptr sink) {
        sink->set_pattern(kLogPattern);
        sSinks.push_back(sink);
        spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) {
            logger->sinks().push_back(sink);
        });
        spdlog::set_level(std::min(sink->level(), spdlog::get_level()));
    }


    void InitLogging() {
        static std::once_flag sOnce;
        std::call_once(sOnce, []{
            // Configure log output:
            spdlog::set_pattern(kLogPattern);
            spdlog::flush_every(5s);
            spdlog::flush_on(spdlog::level::warn);

            // Get the default stderr sink:
            sStderrSink = spdlog::default_logger()->sinks().front();
            sSinks.push_back(sStderrSink);

            // Make the standard loggers:
            LCoro     = makeLogger("Coro");
            LSched    = makeLogger("Sched");
            LLoop     = makeLogger("Loop");
            LNet      = makeLogger("Net");

            // Set log levels from `SPDLOG_LEVEL` env var:
            spdlog::cfg::load_env_levels();

            spdlog::info("---------- Welcome to Crouton ----------");
        });
    }

    shared_ptr<spdlog::logger> MakeLogger(string_view name, spdlog::level::level_enum level) {
        InitLogging();
        return makeLogger(name, level);
    }

    void AddSink(spdlog::sink_ptr sink) {
        InitLogging();
        addSink(sink);
    }

}
