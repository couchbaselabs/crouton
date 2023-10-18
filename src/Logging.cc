//
// Logging.cc
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

#include "Logging.hh"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <mutex>

namespace crouton {
    using namespace std;

    LoggerRef Log, LCoro, LSched, LLoop, LNet;

#if CROUTON_USE_SPDLOG

    /// Defines the format of textual log output.
    static constexpr const char* kLogPattern = "▣ %H:%M:%S.%f %^%L | <%n>%$ %v";
    
    static vector<spdlog::sink_ptr> sSinks;
    static spdlog::sink_ptr         sStderrSink;


    static LoggerRef makeLogger(string_view name, 
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
        return logger.get();
    }
    
    
    static void addSink(spdlog::sink_ptr sink) {
        sink->set_pattern(kLogPattern);
        sSinks.push_back(sink);
        spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) {
            logger->sinks().push_back(sink);
        });
        spdlog::set_level(std::min(sink->level(), spdlog::get_level()));
    }

    
    void AddSink(spdlog::sink_ptr sink) {
        InitLogging();
        addSink(sink);
    }

#else
    static LoggerRef makeLogger(string_view name, LogLevelType level = LogLevel::info) {
        return new Logger();
    }
#endif


    void InitLogging() {
        static std::once_flag sOnce;
        std::call_once(sOnce, []{
#if CROUTON_USE_SPDLOG
            // Configure log output:
            spdlog::set_pattern(kLogPattern);
            spdlog::flush_every(5s);
            spdlog::flush_on(spdlog::level::warn);

            // Get the default stderr sink:
            sStderrSink = spdlog::default_logger()->sinks().front();
            sSinks.push_back(sStderrSink);
            Log       = spdlog::default_logger().get();
#else
            Log       = makeLogger("");
#endif
            // Make the standard loggers:
            LCoro     = makeLogger("Coro");
            LSched    = makeLogger("Sched");
            LLoop     = makeLogger("Loop");
            LNet      = makeLogger("Net");

#if CROUTON_USE_SPDLOG
            // Set log levels from `SPDLOG_LEVEL` env var:
            spdlog::cfg::load_env_levels();
            assert_failed_hook = [](const char* message) {
                spdlog::critical("{}", message);
            };
#endif
            Log->info("---------- Welcome to Crouton ----------");
        });
    }


    LoggerRef MakeLogger(string_view name, LogLevelType level) {
        InitLogging();
        return makeLogger(name, level);
    }
}
