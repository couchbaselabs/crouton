//
// Backtrace+Windows.cc
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

#ifdef _WIN32

#include "Backtrace.hh"
#include <csignal>
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string.h>
#include <algorithm>
#include "util/betterassert.hh"

#pragma comment(lib, "Dbghelp.lib")
#include <Windows.h>
#include <Dbghelp.h>
#include "asprintf.h"
#include <sstream>
using namespace std;

namespace fleece {

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    namespace internal {
        int backtrace(void** buffer, size_t max) {
            return (int)CaptureStackBackTrace(0, (DWORD)max, buffer, nullptr);
        }
    }

    bool Backtrace::writeTo(ostream &out) const {
        const auto process = GetCurrentProcess();
        SYMBOL_INFO *symbol = nullptr;
        IMAGEHLP_LINE64 *line = nullptr;
        bool success = false;
        SymInitialize(process, nullptr, TRUE);
        DWORD symOptions = SymGetOptions();
        symOptions |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
        SymSetOptions(symOptions);

        symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO)+1023 * sizeof(TCHAR));
        if (!symbol)
            goto exit;
        symbol->MaxNameLen = 1024;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        DWORD displacement;
        line = (IMAGEHLP_LINE64*)malloc(sizeof(IMAGEHLP_LINE64));
        if (!line)
            goto exit;
        line->SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        for (unsigned i = 0; i < _addrs.size(); i++) {
            if (i > 0)
                out << "\r\n";
            out << '\t';
            const auto address = (DWORD64)_addrs[i];
            SymFromAddr(process, address, nullptr, symbol);
            char* cstr = nullptr;
            if (SymGetLineFromAddr64(process, address, &displacement, line)) {
                asprintf(&cstr, "at %s in %s: line: %lu: address: 0x%0llX",
                         symbol->Name, line->FileName, line->LineNumber, symbol->Address);
            } else {
                asprintf(&cstr, "at %s, address 0x%0llX",
                         symbol->Name, symbol->Address);
            }
            if (!cstr)
                goto exit;
            out << cstr;
            free(cstr);
        }
        success = true;

    exit:
        free(symbol);
        free(line);
        SymCleanup(process);
        return success;
    }

#else
    // Windows Store apps cannot get backtraces
    namespace internal {
        int backtrace(void** buffer, size_t max) {return 0;}
    }

    bool Backtrace::writeTo(std::ostream&) const  {return false;}
#endif


    namespace internal {
        char* unmangle(const char *function) {
            return (char*)function;
        }
    }


    std::string RawFunctionName(const void *pc) {
        const auto process = GetCurrentProcess();
        auto symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO)+1023 * sizeof(TCHAR));
        if (!symbol)
            return "";
        symbol->MaxNameLen = 1024;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        SymFromAddr(process, (DWORD64)pc, nullptr, symbol);
        std::string result(symbol->Name);
        free(symbol);
        return result;
    }

}

#endif // _WIN32
