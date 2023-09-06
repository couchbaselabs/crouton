//
// Process.hh
//
// 
//

#pragma once
#include "Future.hh"
#include <string_view>
#include <optional>
#include <vector>

namespace crouton {

    /** Main process function that runs an event loop and calls `fn`, which returns a Future.
        When/if the Future resolves, it returns its value as the process status,
        or if the value is an exception it logs an error and returns 1. */
    int Main(int argc, const char* argv[], Future<int> (*fn)());

    /** Main process function that runs an event loop and calls `fn`, which returns a Task.
        The event loop forever or until stopped. */
    int Main(int argc, const char* argv[], Task (*fn)());


    /** Convenience for defining the program's `main` function. */
    #define CROUTON_MAIN(FUNC) \
        int main(int argc, const char* argv[]) {return crouton::Main(argc, argv, FUNC);}



    class Args : public std::vector<std::string_view> {
    public:
        std::optional<std::string_view> first() const;

        std::optional<std::string_view> popFirst();

        std::optional<std::string_view> popFlag();
    };

    /// Process arguments, as captured by Main.
    /// Make a copy if you want to use methods like `popFirst`.
    extern const Args& MainArgs();

}
