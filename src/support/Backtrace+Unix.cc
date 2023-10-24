//
// Backtrace+Unix.cc
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

#ifndef _WIN32

#include "Backtrace.hh"
#include "StringUtils.hh"
#include "util/betterassert.hh"
#include <iostream>

#include <dlfcn.h>          // dladdr()

#ifndef __ANDROID__
    #define HAVE_EXECINFO
    #include <execinfo.h>   // backtrace(), backtrace_symbols()
#else
    #include <stdlib.h>     // free
    #include <unwind.h>     // _Unwind_Backtrace(), etc.
#endif

#ifndef __has_include
#   define __has_include() 0
#endif

#if __has_include(<cxxabi.h>) || defined(__ANDROID__)
    #include <cxxabi.h>     // abi::__cxa_demangle()
    #define HAVE_UNMANGLE
#endif


namespace fleece {
    using namespace std;


    namespace internal {
#ifdef HAVE_EXECINFO
        int backtrace(void** buffer, size_t max) {
            return ::backtrace(buffer, int(max));
        }
#else
        // Use libunwind to emulate backtrace(). This is limited in that it won't traverse any stack
        // frames before a signal handler, so it's not useful for logging the stack upon a crash.
        // Adapted from https://stackoverflow.com/a/28858941
        // See also https://stackoverflow.com/q/29559347 for more details & possible workarounds

        struct BacktraceState {
            void** current;
            void** end;
        };

        static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
            BacktraceState* state = static_cast<BacktraceState*>(arg);
            uintptr_t pc = _Unwind_GetIP(context);
            if (pc) {
                if (state->current == state->end) {
                    return _URC_END_OF_STACK;
                } else {
                    *state->current++ = reinterpret_cast<void*>(pc);
                }
            }
            return _URC_NO_REASON;
        }

        int backtrace(void** buffer, size_t max) {
            BacktraceState state = {buffer, buffer + max};
            _Unwind_Backtrace(unwindCallback, &state);

            return int(state.current - buffer);
        }
#endif

        
        char* unmangle(const char *function) {
#ifdef HAVE_UNMANGLE
            int status;
            size_t unmangledLen;
            char *unmangled = abi::__cxa_demangle(function, nullptr, &unmangledLen, &status);
            if (unmangled && status == 0)
                return unmangled;
            free(unmangled);
#endif
            return (char*)function;
        }
    }


    Backtrace::frameInfo Backtrace::getFrame(unsigned i) const {
        precondition(i < _addrs.size());
        frameInfo frame = { };
        Dl_info info;
        if (dladdr(_addrs[i], &info)) {
            frame.pc = _addrs[i];
            frame.offset = (size_t)frame.pc - (size_t)info.dli_saddr;
            frame.function = info.dli_sname;
            frame.library = info.dli_fname;
            const char *slash = strrchr(frame.library, '/');
            if (slash)
                frame.library = slash + 1;
        }
        return frame;
    }


    // If any of these strings occur in a backtrace, suppress further frames.
    static constexpr const char* kTerminalFunctions[] = {
        "_C_A_T_C_H____T_E_S_T_",
        "Catch::TestInvokerAsFunction::invoke() const",
        "litecore::actor::Scheduler::task(unsigned)",
        "litecore::actor::GCDMailbox::safelyCall",
    };

    static constexpr struct {const char *old, *nuu;} kAbbreviations[] = {
        {"(anonymous namespace)",   "(anon)"},
        {"std::__1::",              "std::"},
        {"std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
                                    "string"},
    };


    bool Backtrace::writeTo(ostream &out) const {
        for (unsigned i = 0; i < _addrs.size(); ++i) {
            if (i > 0)
                out << '\n';
            out << '\t';
            char *cstr = nullptr;
            auto frame = getFrame(i);
            int len;
            bool stop = false;
            if (frame.function) {
                string name = Unmangle(frame.function);
                // Stop when we hit a unit test, or other known functions:
                for (auto fn : kTerminalFunctions) {
                    if (name.find(fn) != string::npos)
                        stop = true;
                }
                // Abbreviate some C++ verbosity:
                for (auto &abbrev : kAbbreviations)
                    crouton::replaceStringInPlace(name, abbrev.old, abbrev.nuu);
                len = asprintf(&cstr, "%2d  %-25s %s + %zd",
                               i, frame.library, name.c_str(), frame.offset);
            } else {
                len = asprintf(&cstr, "%2d  %p", i, _addrs[i]);
            }
            if (len < 0)
                return false;
            out.write(cstr, size_t(len));
            free(cstr);

            if (stop) {
                out << "\n\t ... (" << (_addrs.size() - i - 1) << " more suppressed) ...";
                break;
            }
        }
        return true;
    }


    std::string RawFunctionName(const void *pc) {
        Dl_info info = {};
        dladdr(pc, &info);
        return info.dli_sname;
    }

}

#endif // _WIN32
