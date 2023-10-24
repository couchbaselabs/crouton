//
// Backtrace.cc
//
// Copyright © 2018-Present Couchbase, Inc. All rights reserved.
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

#include "Backtrace.hh"
#include <csignal>
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string.h>
#include <algorithm>
#include "util/betterassert.hh"


#pragma mark - COMMON CODE:


namespace fleece {
    using namespace std;


    string Unmangle(const char *name) {
        char* unmangled = internal::unmangle(name);
        string result = unmangled;
        if (unmangled != name)
            free(unmangled);
        return result;
    }


    string Unmangle(const std::type_info &type) {
        return Unmangle(type.name());
    }


    std::string FunctionName(const void *pc) {
        if (std::string raw = RawFunctionName(pc); !raw.empty())
            return Unmangle(raw.c_str());
        else
            return "";
    }


    shared_ptr<Backtrace> Backtrace::capture(unsigned skipFrames, unsigned maxFrames) {
        // (By capturing afterwards, we avoid the (many) stack frames associated with make_shared)
        auto bt = make_shared<Backtrace>(0, 0);
        bt->_capture(skipFrames + 1, maxFrames);
        return bt;
    }

    Backtrace::Backtrace(unsigned skipFrames, unsigned maxFrames) {
        if (maxFrames > 0)
            _capture(skipFrames + 1, maxFrames);
    }


    void Backtrace::_capture(unsigned skipFrames, unsigned maxFrames) {
        _addrs.resize(++skipFrames + maxFrames);        // skip this frame
        auto n = internal::backtrace(&_addrs[0], skipFrames + maxFrames);
        _addrs.resize(n);
        skip(skipFrames);
    }


    void Backtrace::skip(unsigned nFrames) {
        _addrs.erase(_addrs.begin(), _addrs.begin() + min(size_t(nFrames), _addrs.size()));
    }


    string Backtrace::toString() const {
        stringstream out;
        writeTo(out);
        return out.str();
    }


    void Backtrace::writeCrashLog(ostream &out) {
        Backtrace bt(4);
        auto xp = current_exception();
        if (xp) {
            out << "Uncaught exception:\n\t";
            try {
                rethrow_exception(xp);
            } catch(const exception& x) {
#if __cpp_rtti
                const char *name = typeid(x).name();
                char *unmangled = internal::unmangle(name);
                out << unmangled << ": " <<  x.what() << "\n";
                if (unmangled != name)
                    free(unmangled);
#else
                out << x.what() << "\n";
#endif
            } catch (...) {
                out << "unknown exception type\n";
            }
        }
        out << "Backtrace:";
        bt.writeTo(out);
    }


    void Backtrace::installTerminateHandler(function<void(const string&)> logger) {
        static once_flag sOnce;
        call_once(sOnce, [&] {
            static auto const sLogger = std::move(logger);
            static terminate_handler const sOldHandler = set_terminate([] {
                // ---- Code below gets called by C++ runtime on an uncaught exception ---
                if (sLogger) {
                    stringstream out;
                    writeCrashLog(out);
                    sLogger(out.str());
                } else {
                    cerr << "\n\n******************** C++ fatal error ********************\n";
                    writeCrashLog(cerr);
                    cerr << "\n******************** Now terminating ********************\n";
                }
                // Chain to old handler:
                sOldHandler();
                // Just in case the old handler doesn't abort:
                abort();
                // ---- End of handler ----
            });
        });
    }

}
