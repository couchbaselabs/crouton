//
// Process.cc
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

#include "io/Process.hh"
#include "util/Logging.hh"
#include "Task.hh"
#include <cstdio>

#ifndef ESP_PLATFORM
#include "uv/UVInternal.hh"
#endif

#if defined(_MSC_VER)
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <io.h>
#  include <conio.h>
#  define isatty _isatty
#elif defined(ESP_PLATFORM)
#   include <esp_system.h>
#   include <thread>
#else
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

namespace crouton::io {
    using namespace std;


    Args sArgs;


    static void initArgs(int argc, const char * argv[]) {
#ifdef ESP_PLATFORM
        auto nuArgv = argv;
#else
        auto nuArgv = uv_setup_args(argc, (char**)argv);
#endif
        sArgs.resize(argc);
        for (int i = 0; i < argc; ++i)
            sArgs[i] = nuArgv[i];
    }

    Args const& MainArgs() {
        return sArgs;
    }

    optional<string_view> Args::first() const {
        optional<string_view> arg;
        if (size() >= 1)
            arg = at(1);
        return arg;
    }

    optional<string_view> Args::popFirst() {
        optional<string_view> arg;
        if (size() >= 1) {
            arg = std::move(at(1));
            erase(begin() + 1);
        }
        return arg;
    }

    optional<string_view> Args::popFlag() {
        if (auto flag = first(); flag && flag->starts_with("-")) {
            popFirst();
            return flag;
        } else {
            return nullopt;
        }
    }



    int Main(int argc, const char * argv[], Future<int>(*fn)()) {
#ifdef ESP_PLATFORM
        std::thread([&] {
#endif
        try {
            initArgs(argc, argv);
            InitLogging();
            Future<int> fut = fn();
            Scheduler::current().runUntil([&]{ return fut.hasResult(); });
            return fut.result();
        } catch (std::exception const& x) {
            Log->error("*** Unexpected exception: {}" , x.what());
            return 1;
        } catch (...) {
            Log->error("*** Unexpected exception");
            return 1;
        }
#ifdef ESP_PLATFORM
        }).join();
        printf(" Restarting now.\n");
        fflush(stdout);
        esp_restart();
#endif
    }

    int Main(int argc, const char * argv[], Task(*fn)()) {
#ifdef ESP_PLATFORM
        std::thread([&] {
#endif
        try {
            initArgs(argc, argv);
            Task task = fn();
            Scheduler::current().run();
            return 0;
        } catch (std::exception const& x) {
            Log->error("*** Unexpected exception: {}", x.what());
            return 1;
        } catch (...) {
            Log->error("*** Unexpected exception");
            return 1;
        }
#ifdef ESP_PLATFORM
        }).join();
        printf(" Restarting now.\n");
        fflush(stdout);
        esp_restart();
#endif
    }


#pragma mark - TTY STUFF:


#ifdef _MSC_VER
    typedef LONG NTSTATUS, *PNTSTATUS;
#define STATUS_SUCCESS (0x00000000)

    typedef NTSTATUS (WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

    RTL_OSVERSIONINFOW GetRealOSVersion() {
        HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
        if (hMod) {
            RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
            if (fxPtr != nullptr) {
                RTL_OSVERSIONINFOW rovi = { 0 };
                rovi.dwOSVersionInfoSize = sizeof(rovi);
                if ( STATUS_SUCCESS == fxPtr(&rovi) ) {
                    return rovi;
                }
            }
        }
        RTL_OSVERSIONINFOW rovi = { 0 };
        return rovi;
    }
#endif


    static bool isColor(int fd) {
#ifdef ESP_PLATFORM
    #if CONFIG_LOG_COLORS
        return true;
    #else
        return false;
    #endif
#else
        if (getenv("CLICOLOR_FORCE")) {
            return true;
        }

        if (!isatty(fd))
            return false;

        if (const char *term = getenv("TERM")) {
            if (strstr(term,"ANSI") || strstr(term,"ansi") || strstr(term,"color"))
                return true;
        }

#ifdef _MSC_VER
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        // Sick of this being missing for whatever reason
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#endif
        if (GetRealOSVersion().dwMajorVersion >= 10) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD consoleMode;
            if(GetConsoleMode(hConsole, &consoleMode)) {
                SetConsoleMode(hConsole, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
            return true;
        }
#endif

        return false;
#endif
    }


    // <https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_(Select_Graphic_Rendition)_parameters>
    #define ANSI "\033["

    TTY::TTY(int fd)
    :color(isColor(fd))
    ,bold       (color ? ANSI "1m"  : "")
    ,dim        (color ? ANSI "2m"  : "")
    ,italic     (color ? ANSI "3m"  : "")
    ,underline  (color ? ANSI "4m"  : "")
    ,red        (color ? ANSI "31m" : "")
    ,yellow     (color ? ANSI "33m" : "")
    ,green      (color ? ANSI "32m" : "")
    ,reset      (color ? ANSI "0m"  : "")
    { }


    TTY const TTY::out(1);
    TTY const TTY::err(2);

}
