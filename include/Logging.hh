//
// Logging.hh
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

#pragma once
#include "util/Base.hh"

#ifdef ESP_PLATFORM
#define CROUTON_USE_SPDLOG 0
#endif

#ifndef CROUTON_USE_SPDLOG
#define CROUTON_USE_SPDLOG 1
#endif

#if CROUTON_USE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>    // Makes custom types loggable via `operator <<` overloads
#endif

namespace crouton {

#if CROUTON_USE_SPDLOG
    using LoggerRef = spdlog::logger*;
    namespace LogLevel = spdlog::level;
    using LogLevelType = spdlog::level::level_enum;
#else
    namespace LogLevel {
        enum level_enum {
            trace, debug, info, warn, err, critical, off
        };
    };
    using LogLevelType = LogLevel::level_enum;

    class Logger {
    public:
        LogLevelType level() const {
            return LogLevel::off;
        }
        void set_level(LogLevelType) const {
        }
        bool should_log(LogLevelType msg_level) const {
            return false;
        }
        template<typename... Args>
        void log(LogLevelType lvl, string_view fmt, Args &&...args) {
            
        }
        void log(LogLevelType lvl, string_view msg)
        {
        }
        template<typename... Args>
        void trace(string_view fmt, Args &&...args)
        {
            log(LogLevel::trace, fmt, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void debug(string_view fmt, Args &&...args)
        {
            log(LogLevel::debug, fmt, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void info(string_view fmt, Args &&...args)
        {
            log(LogLevel::info, fmt, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void warn(string_view fmt, Args &&...args)
        {
            log(LogLevel::warn, fmt, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void error(string_view fmt, Args &&...args)
        {
            log(LogLevel::err, fmt, std::forward<Args>(args)...);
        }
        template<typename... Args>
        void critical(string_view fmt, Args &&...args)
        {
            log(LogLevel::critical, fmt, std::forward<Args>(args)...);
        }
    };
    using LoggerRef = Logger*;
#endif

    /*
     You can configure the log level(s) by setting the environment variable `SPDLOG_LEVEL`.
     For example:

        * Set global level to debug:
            `export SPDLOG_LEVEL=debug`
        * Turn off all logging except for logger1:
            `export SPDLOG_LEVEL="*=off,logger1=debug"`
        * Turn off all logging except for logger1 and logger2:
            `export SPDLOG_LEVEL="off,logger1=debug,logger2=info"`

        For much more information about spdlog, see docs at https://github.com/gabime/spdlog/
     */


    /// Initializes spdlog logging, sets log levels and creates well-known loggers.
    /// Called automatically by `MakeLogger` and `AddSink`.
    /// Calling this multiple times has no effect.
    void InitLogging();


    /// Well-known loggers:
    extern LoggerRef
        Log,    // Default logger
        LCoro,  // Coroutine lifecycle
        LSched, // Scheduler
        LLoop,  // Event-loop
        LNet;   // Network I/O


    /// Creates a new spdlog logger.
    LoggerRef MakeLogger(string_view name, LogLevelType = LogLevel::info);

#if CROUTON_USE_SPDLOG
    /// Creates a log destination.
    void AddSink(spdlog::sink_ptr);
#endif
}
