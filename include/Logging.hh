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
#include "Base.hh"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>    // Makes custom types loggable via `operator <<` overloads

namespace crouton {

    /*
     You can configure the log level(s) by setting the environment variable `SPDLOG_LEVEL`.
     For example:

        * Set global level to debug:
            `export SPDLOG_LEVEL=debug`
        * Turn off all logging except for logger1:
            `export SPDLOG_LEVEL="*=off,logger1=debug"`
        * Turn off all logging except for logger1 and logger2:
            `export SPDLOG_LEVEL="off,logger1=debug,logger2=info"`
     */


    /// Initializes logging, sets log levels and creates well-known loggers.
    /// Called automatically by `MakeLogger` and `AddSink`.
    /// Calling this multiple times has no effect.
    void InitLogging();


    /// Well-known loggers:
    extern std::shared_ptr<spdlog::logger>
        LCoro,  // Coroutine lifecycle
        LSched, // Scheduler logs
        LLoop,  // Event-loop
        LNet;   // Network I/O


    /// Creates a new spdlog logger.
    std::shared_ptr<spdlog::logger> MakeLogger(string_view name,
                                               spdlog::level::level_enum = spdlog::level::info);

    /// Creates a log destination.
    void AddSink(spdlog::sink_ptr);
}
