//
// CoroLifecycle.cc
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

#include "CoroLifecycle.hh"
#include "Logging.hh"
#include "Memoized.hh"
#include "Scheduler.hh"
#include <mutex>
#include <ranges>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crouton::lifecycle {
    using namespace std;


#if CROUTON_LIFECYCLES


#pragma mark - COROUTINE INFO STRUCT:


    enum class coroState { born, active, awaiting, yielding, ending };

    static constexpr const char* kStateNames[] = {"born", "active", "awaiting", "yielding", "ending"};

    // If true, the sCoros table will remember destroyed/deleted coroutines, so if a destroyed
    // coroutine handle is accessed it can tell you its former sequence number and owner.
    // The downside is that the table uses more memory over time since it never shrinks.
    static constexpr bool kRememberDestroyedCoros = true;

    static unsigned sLastSequence = 0;

    static string_view getCoroTypeName(std::type_info const& type) {
        string const& fullName = GetTypeName(type);
        string_view name = fullName;
        if (auto p = name.find('<'); p != string::npos)         // Strip template params
            name = name.substr(0, p);
        if (name.ends_with("Impl"))                             // Strip -Impl suffix
            name = name.substr(0, name.size() - 4);
        return name;
    }

    /// Metadata about a coroutine
    struct coroInfo {
        coroInfo(coro_handle h, std::type_info const& impl)
        :handle(h)
        ,sequence(++sLastSequence)
        ,name(CoroutineName(h))
        ,typeName(getCoroTypeName(impl))
        { }

        coroInfo(coroInfo const&) = delete;
        coroInfo& operator=(coroInfo const&) = delete;
        coroInfo(coroInfo&&) = default;
        coroInfo& operator=(coroInfo&&) = default;

        void setState(coroState s) {
            if (state == coroState::awaiting || state == coroState::yielding) {
                awaitingCoro = nullptr;
                awaiting = nullptr;
                awaitingType = nullptr;
            }
            state = s;
        }

        friend ostream& operator<< (ostream& out, coroInfo const& info) {
            return out << "¢" << info.sequence;
        }

        coro_handle         handle;                     // Its handle; nullptr if tombstone
        coroState           state = coroState::born;    // Current state
        coroInfo*           caller = nullptr;           // Caller, if on stack
        coro_handle         awaitingCoro;               // Coro it's awaiting
        const void*         awaiting = nullptr;         // Other object it's awaiting
        const type_info*    awaitingType = nullptr;     // Type of that other object
        unsigned            sequence;                   // Serial number, starting at 1
        string              name;                       // Its function name
        string_view         typeName;                   // Its class name
        bool                ignoreInCount = false;      // Don't include this in `count()`
    };


    /// Wraps a coroInfo for logging, and prints out the full name
    struct verbose {
        coroInfo& info;
        friend ostream& operator<< (ostream& out, verbose const& v) {
            return out << "¢" << v.info.sequence
                       << " [" << v.info.typeName << ' ' << v.info.name << "()]"
                       << " " << (void*)v.info.handle.address()
            ;
        }
    };


    static mutex sCorosMutex;
    static unordered_map<void*, coroInfo> sCoros;   // Maps coro_handle -> coroInfo

    /// Gets a coro_handle's coroInfo. Mutex must be locked.
    static auto _getInfoIter(coro_handle h) {
        auto i = sCoros.find(h.address());
        if (i == sCoros.end()) {
            LCoro->critical("FATAL: Unknown coroutine_handle {}", h.address());
            abort();
        } else if (i->second.handle == nullptr) {
            LCoro->critical("FATAL: Using destroyed coroutine_handle {}; formerly ¢{} [{} {}]",
                            h.address(), i->second.sequence, i->second.typeName, i->second.name);
            abort();
        } else {
            return i;
        }
    }

    static coroInfo& _getInfo(coro_handle h)  {return _getInfoIter(h)->second;}

    /// Gets a coro_handle's coroInfo.
    static coroInfo& getInfo(coro_handle h) {
        unique_lock<mutex> lock(sCorosMutex);
        return _getInfo(h);
    }

    /// Gets a coro_handle's sequence
    unsigned getSequence(coro_handle h) {
        return isNoop(h) ? 0 : getInfo(h).sequence;
    }

    /// Indicates this coroutine should be ignored by `count()`
    void ignoreInCount(coro_handle h) {
        getInfo(h).ignoreInCount = true;
    }

    /// Returns number of coroutines. Mutex must be locked.
    static size_t _count() {
        size_t n = 0;
        for (auto &[addr, info] : sCoros) {
            if (info.handle && !info.ignoreInCount)
                ++n;
        }
        return n;
    }

    /// Returns number of coroutines.
    size_t count() {
        unique_lock<mutex> lock(sCorosMutex);
        return _count();
    }


#pragma mark - ACTIVE-COROUTINE STACK:


    // Tracks current coroutine, per thread.
    thread_local coroInfo* tCurrent = nullptr;


    /// Logs the coroutines in the current thread's stack.
    static void logStack() {
        if (LCoro->should_log(spdlog::level::trace)) {
            stringstream out;
            for (auto c = tCurrent; c; c = c->caller)
                out << ' ' << *c;
            LCoro->trace("CURRENT: {}", out.str());
        }
    }

    /// Returns the coroutine this one called.
    static coroInfo* _currentCalleeOf(coroInfo &caller) {
        for (auto &[h, info] : sCoros) {
            if (info.caller == &caller && info.handle)
                return &info;
        }
        return nullptr;
    }

    /// Asserts that this is the current coroutine (of this thread.)
    static void assertCurrent(coroInfo &h) {
        if (tCurrent != &h) {
            LCoro->warn("??? Expected {} to be current, not {}", *tCurrent, h);
            assert(tCurrent == &h);
        }
    }

    /// Pushes a coroutine onto the current thread's stack.
    static void pushCurrent(coroInfo &info) {
        assert(!info.caller);
        info.caller = tCurrent;
        tCurrent = &info;
        logStack();
    }

    /// Pops the top coroutine from the current thread's stack.
    static void popCurrent(coroInfo &cur) {
        assertCurrent(cur);
        tCurrent = cur.caller;
        cur.caller = nullptr;
        logStack();
    }

    /// Replaces the top coroutine with a different one, or just pops it.
    static void switchCurrent(coroInfo &cur, coro_handle nextHandle) {
        assert(cur.handle != nextHandle);
        assertCurrent(cur);
        tCurrent = cur.caller;
        cur.caller = nullptr;
        if (!isNoop(nextHandle)) {
            auto& next = getInfo(nextHandle);
            assert(!next.caller);
            next.caller = tCurrent;
            tCurrent = &next;
        }
        logStack();
    }

    /// Returns the depth of the stack.
    size_t stackDepth() {
        size_t depth = 0;
        for (auto i = tCurrent; i; i = i->caller)
            ++depth;
        return depth;
    }


#pragma mark - LIFECYCLE CALLS:


    void created(coro_handle h, bool ready, std::type_info const& implType) {
        precondition(h);
        unique_lock<mutex> lock(sCorosMutex);
        auto [i, added] = sCoros.try_emplace(h.address(), h, implType);
        if (kRememberDestroyedCoros) {
            if (!added) {
                // Reusing a tombstone when a new coro is allocated at the same address:
                assert(!i->second.handle);
                i->second = coroInfo{h, implType};
            }
        } else {
            assert(added);
        }
        lock.unlock();
        
        if (ready) {
            i->second.setState(coroState::active);
            LCoro->debug("{} created and starting", verbose{i->second});
            pushCurrent(i->second);
        } else {
            LCoro->debug("{} created", verbose{i->second});
        }
    }

    void suspendInitial(coro_handle cur) {
        auto& curInfo = getInfo(cur);
        LCoro->trace("{} initially suspended", curInfo);
        if (curInfo.state == coroState::active) {
            popCurrent(curInfo);
            curInfo.setState(coroState::born);
        }
    }

    void ready(coro_handle h) {
        auto& curInfo = getInfo(h);
        LCoro->trace("{} starting", curInfo);

        assert(curInfo.state == coroState::born);
        curInfo.setState(coroState::active);
        pushCurrent(curInfo);
    }

    static coro_handle switching(coroInfo& curInfo, coro_handle next) {
        if (next && !isNoop(next)) {
            auto& nextInfo = getInfo(next);
            LCoro->trace("{} resuming", nextInfo);
            assert(nextInfo.state != coroState::active);
            nextInfo.setState(coroState::active);
            nextInfo.awaiting = nullptr;
        }
        switchCurrent(curInfo, next);
        return next ? next : CORO_NS::noop_coroutine();
    }

    coro_handle suspendingTo(coro_handle cur,
                             std::type_info const& toType, const void* to,
                             coro_handle next)
    {
        auto& curInfo = getInfo(cur);
        assertCurrent(curInfo);
        if (cur == next)
            return next;

        if (to) {
            LCoro->trace("{} awaiting {} {}", curInfo, GetTypeName(toType), to);
        }
        assert(curInfo.state == coroState::active);
        curInfo.setState(coroState::awaiting);
        curInfo.awaiting = to;
        curInfo.awaitingType = &toType;

        return switching(curInfo, next);
    }

    coro_handle suspendingTo(coro_handle cur,
                             coro_handle awaitingCoro,
                             coro_handle next)
    {
        auto& curInfo = getInfo(cur);
        assertCurrent(curInfo);
        if (cur == next)
            return next;

        LCoro->trace("{} awaiting {}", curInfo, logCoro{awaitingCoro});
        assert(curInfo.state == coroState::active);
        curInfo.setState(coroState::awaiting);
        curInfo.awaitingCoro = awaitingCoro;

        return switching(curInfo, next);
    }

    coro_handle yieldingTo(coro_handle cur, coro_handle next, bool isCall) {
        auto& curInfo = getInfo(cur);
        assertCurrent(curInfo);
        if (cur == next)
            return next;

        LCoro->trace("{} yielded to {}", curInfo, logCoro{next});
        assert(curInfo.state == coroState::active);
        curInfo.setState(coroState::yielding);
        if (isCall && !isNoop(next))
            curInfo.awaitingCoro = next;

        return switching(curInfo, next);
    }

    coro_handle finalSuspend(coro_handle cur, coro_handle next) {
        auto& curInfo = getInfo(cur);
        assertCurrent(curInfo);
        
        if (curInfo.state < coroState::ending) {
            LCoro->trace("{} finished", curInfo);
            curInfo.setState(coroState::ending);
        }

        return switching(curInfo, next);
    }

    void resume(coro_handle h) {
        auto& curInfo = getInfo(h);
        LCoro->trace("{}.resume() ...", curInfo);

        pushCurrent(curInfo);

        assert(curInfo.state != coroState::active && curInfo.state != coroState::ending);
        curInfo.setState(coroState::active);
        curInfo.awaiting = nullptr;

        h.resume();

        LCoro->trace("...After resume()");
    }

    void threw(coro_handle h) {
        auto& curInfo = getInfo(h);
        assertCurrent(curInfo);
        LCoro->error("{} threw an exception.", curInfo);
        curInfo.setState(coroState::ending);
    }

    void returning(coro_handle h) {
        auto& curInfo = getInfo(h);
        assertCurrent(curInfo);
        LCoro->trace("{} returned.", curInfo);
        curInfo.setState(coroState::ending);
    }

    void ended(coro_handle h) {
        unique_lock<mutex> lock(sCorosMutex);
        auto i = _getInfoIter(h);
        auto& info = i->second;

        assert(&info != tCurrent);
        assert(!info.caller);
        for (auto &other : sCoros)
            assert(other.second.caller != &info);

        if (info.state < coroState::ending)
            LCoro->warn("{} destructed before returning or throwing", info);
        LCoro->debug("{} destructed. ({} left)", info, _count() - 1);
        
        if (kRememberDestroyedCoros) {
            i->second.handle = nullptr; // mark as tombstone
        } else {
            sCoros.erase(i);
        }
    }

    void destroy(coro_handle h) {
        h.destroy();
    }


#pragma mark - LOGGING:


    // logs all coroutines, in order they were created
    void logAll() {
        using enum coroState;
        unique_lock<mutex> lock(sCorosMutex);
        vector<coroInfo*> infos;
        for (auto &[addr, info] : sCoros) {
            if (info.handle)
                infos.emplace_back(&info);
        }
        sort(infos.begin(), infos.end(), [](auto a, auto b) {return a->sequence < b->sequence;});
        LCoro->info("{} Existing Coroutines:", infos.size());
        for (auto info : infos) {
            switch (info->state) {
                case born:
                    LCoro->info("\t{} [born]", verbose{*info});
                    break;
                case active:
                    if (coroInfo* calling = _currentCalleeOf(*info))
                        LCoro->info("\t{} -> calling ¢{}", verbose{*info}, calling->sequence);
                    else
                        LCoro->info("\t{} **CURRENT**", verbose{*info});
                    break;
                case awaiting:
                    if (info->awaitingCoro)
                        LCoro->info("\t{} -> awaiting ¢{}", verbose{*info},
                                    _getInfo(info->awaitingCoro).sequence);
                    else if (info->awaiting)
                        LCoro->info("\t{} -> awaiting {} {}", verbose{*info}, GetTypeName(*info->awaitingType), info->awaiting);
                    else
                        LCoro->info("\t{} -> awaiting <?>", verbose{*info});
                    break;
                case yielding:
                    if (info->awaitingCoro)
                        LCoro->info("\t{} -> yielding to ¢{}", verbose{*info},
                                    _getInfo(info->awaitingCoro).sequence);
                    else
                        LCoro->info("\t{} -- yielding", verbose{*info});
                    break;
                case ending:
                    LCoro->info("\t{} [ending]", verbose{*info});
                    break;
            }
        }
    }


    void logStacks() {
        using enum coroState;
        unique_lock<mutex> lock(sCorosMutex);
        size_t n = std::ranges::count_if(sCoros, [](auto &p) {return p.second.handle != nullptr;});
        LCoro->info("{} Existing Coroutines, By Stack:", n);

        unordered_set<coroInfo*> remaining;
        unordered_map<coroInfo*,coroInfo*> next;    // next[c] = the caller/awaiter of c
        for (auto &[h,c] : sCoros) {
            if (c.handle) {
                remaining.insert(&c);
                if (c.state == active && c.caller) {
                    next.insert({&c, c.caller});
                } else if ((c.state == awaiting || c.state == yielding) && c.awaitingCoro) {
                    coroInfo& other = _getInfo(c.awaitingCoro);
                    next[&other] = &c;
                }
            }
        }

        auto printStack = [&](coroInfo &c) {
            int depth = 1;
            if (c.state == active) {
                LCoro->info("        {}: {}", depth, verbose{c});
            } else if (c.awaiting) {
                LCoro->info("        {}: {} (awaiting {} {})", depth, verbose{c},
                            GetTypeName(*c.awaitingType), c.awaiting);
            } else {
                LCoro->info("        {}: {} ({})", depth, verbose{c}, kStateNames[int(c.state)]);
            }
            remaining.erase(&c);

            coroInfo* cur = &c;
            while (true) {
                if (auto i = next.find(cur); i != next.end()) {
                    cur = i->second;
                    LCoro->info("        {}: {} ({})", ++depth, verbose{*cur}, kStateNames[int(cur->state)]);
                    remaining.erase(cur);
                } else {
                    break;
                }
            }
        };

        if (tCurrent) {
            LCoro->info("    Current stack:");
            printStack(*tCurrent);
        }

        for (auto &[h,c] : sCoros) {
            if (remaining.contains(&c) && next.contains(&c)
                    && (c.state == active || !c.awaitingCoro)) {
                LCoro->info("    Stack:");
                printStack(c);
            }
        }

        if (!remaining.empty()) {
            LCoro->info("    Others:");
            for (coroInfo *c : remaining)
                LCoro->info("        -  {} ({})", verbose{*c}, kStateNames[int(c->state)]);
        }
    }

#endif // CROUTON_LIFECYCLES
}


#if CROUTON_LIFECYCLES
void dumpCoros()            {crouton::lifecycle::logAll();}
void dumpCoroStacks()       {crouton::lifecycle::logStacks();}
#endif
