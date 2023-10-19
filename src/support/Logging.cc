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

#include "util/Logging.hh"
#include "io/Process.hh"

#if CROUTON_USE_SPDLOG
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
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

#else // CROUTON_USE_SPDLOG

#ifndef ESP_PLATFORM
    static mutex        sLogMutex;      // makes logging thread-safe and prevents overlapping msgs
    static std::time_t  sTime;          // Time in seconds that's formatted in sTimeBuf
    static char         sTimeBuf[30];   // Formatted timestamp, to second accuracy

    static constexpr const char* kLevelName[] = {
        "trace", "debug", "info ", "WARN ", "ERR  ", "CRITICAL", ""
    };

    void Logger::_writeHeader(LogLevelType lvl) {
        // sLogMutex must be locked
        timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec != sTime) {
            sTime = now.tv_sec;
            tm nowStruct;
            localtime_r(&sTime, &nowStruct);
            strcpy(sTimeBuf, "▣ ");
            strcat(sTimeBuf, io::TTY::err.dim);
            size_t len = strlen(sTimeBuf);
            strftime(sTimeBuf + len, sizeof(sTimeBuf) - len, "%H:%M:%S.", &nowStruct);
        }

        const char* color = "";
        if (lvl >= LogLevel::err)
            color = io::TTY::err.red;
        else if (lvl == LogLevel::warn)
            color = io::TTY::err.yellow;

        fprintf(stderr, "%s%06ld%s %s%s| <%s> ",
                sTimeBuf, now.tv_nsec / 1000, io::TTY::err.reset,
                color, kLevelName[int(lvl)], _name.c_str());
    }


    void Logger::log(LogLevelType lvl, string_view msg) {
        if (should_log(lvl)) {
            unique_lock<mutex> lock(sLogMutex);
            _writeHeader(lvl);
            cerr << msg << io::TTY::err.reset << std::endl;
        }
    }


    void Logger::_log(LogLevelType lvl, string_view fmt, minifmt::FmtIDList types, ...) {
        unique_lock<mutex> lock(sLogMutex);

        _writeHeader(lvl);
        va_list args;
        va_start(args, types);
        minifmt::vformat_types(cerr, fmt, types, args);
        va_end(args);
        cerr << io::TTY::err.reset << endl;
    }

#else
    static constexpr esp_log_level_t kESPLevel[] = {
        ESP_LOG_VERBOSE, ESP_LOG_DEBUG, ESP_LOG_INFO, ESP_LOG_WARN, ESP_LOG_ERROR, ESP_LOG_NONE
    };
    static const char* kESPLevelChar = "TDIWE-";


    void Logger::log(LogLevelType lvl, string_view msg) {
        if (should_log(lvl) && kESPLevel[lvl] <= esp_log_level_get("Crouton")) {
            const char* color;
            switch (lvl) {
                case LogLevel::critical:
                case LogLevel::err:     color = io::TTY::err.red; break;
                case LogLevel::warn:    color = io::TTY::err.yellow; break;
                case LogLevel::debug:
                case LogLevel::trace:   color = io::TTY::err.dim; break;
                default:                color = ""; break;
            }
#if CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
            esp_log_write(kESPLevel[lvl], "Crouton", "%s%c (%4ld) <%s> %.*s%s\n",
                          color,
                          kESPLevelChar[lvl],
                          esp_log_timestamp(),
                          _name.c_str(),
                          int(msg.size()), msg.data(),
                          io::TTY::err.reset);
#else
            esp_log_write(kESPLevel[lvl], "Crouton", "%s%c (%s) <%s> %.*s%s\n",
                          color,
                          kESPLevelChar[lvl],
                          esp_log_system_timestamp(),
                          _name.c_str(),
                          int(msg.size()), msg.data(),
                          io::TTY::err.reset);
#endif
        }
    }

    void Logger::_log(LogLevelType lvl, string_view fmt, minifmt::FmtIDList types, ...) {
        va_list args;
        va_start(args, types);
        string message = minifmt::vformat_types(fmt, types, args);
        va_end(args);
        log(lvl, message);
    }
#endif // ESP_PLATFORM

    static LoggerRef makeLogger(string_view name, LogLevelType level = LogLevel::info) {
        return new Logger(string(name), level);
    }
#endif // CROUTON_USE_SPDLOG


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
#endif
            assert_failed_hook = [](const char* message) {
                Log->critical(message);
            };
            Log->info("---------- Welcome to Crouton ----------");
        });
    }


    LoggerRef MakeLogger(string_view name, LogLevelType level) {
        InitLogging();
        return makeLogger(name, level);
    }
}
