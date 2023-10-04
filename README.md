#  Crouton

Crouton is a C++20 coroutine runtime library that provides some general purpose utilities, as well as cross-platform event loops, I/O and networking based on the [libuv][LIBUV], [mbedTLS][MBEDTLS] and [llhttp][LLHTTP] libraries. (On Apple platforms it can also use the system Network.framework.)

A **Coroutine** is _a function that can return partway through, and then later be resumed where it left off._ Knuth wrote about them in the 1960s and they remained a curiosity for a long time, but they've since become widely used under the hood of the "async / await" concurrency model used in languages like JavaScript, C#, Rust, Nim, and Swift. Crouton brings this to C++.

Async/await gives you concurrency without the pitfalls of multithreading. You can write code in a linear fashion, "blocking" on slow operations like I/O (see the [example](#Example) below), but the thread doesn't really block: instead your (coroutine) function is suspended and some other function that's finished blocking can resume. When the I/O completes, your function is resumed transparently at the next opportunity.

How is that better than threads? It's safer and easier to reason about. The only places where concurrency happens are well-marked by the `co_await` and `co_yield` keywords. You don't need mutexes or atomic variables, and there are far fewer opportunities for race conditions or deadlocks. (There are performance benefits too: no expensive context switches, less stack usage.)

[Detailed documentation](docs/README.md) is being written.

## Features

* Coroutine library:
    * Useful base classes for implementing new coroutine types
    * `Future`, an asynchronous promise type
    * `Generator`, an iterator implementation based on `co_yield`-ing values
    * `CoMutex`, `CoCondition`, `Blocker`: cooperative equivalents of common sync primitives
    * `AsyncQueue`, a producer/consumer queue
    * `Select`, a way to await multiple things in parallel
    * Optional coroutine lifecycle tracking, with debugging utilities to dump all existing
      coroutines and their "call stacks".

* Event loops:
    * `Scheduler`, which manages multiple active coroutines on a thread
        * Round-robin scheduling of multiple active coroutines
        * Suspending a coroutine, then waking it when it's ready
        * Scheduling a function to run on the next event-loop iteration, even on a different thread
        * Scheduling a function to run on a background thread-pool
    * `Task`, an independently-running coroutine that can `co_yield` to give time to others
    * `Timer`, repeating or one-shot

* Reactive publish/subscribe framework
    * Enables building complex networked data flows out of modular components
    * Intrinsically supports backpressure to manage flow control on sockets
    * Loosely inspired by Apple's Combine framework
    * Double-plus experimental ⚗️
    
* Asynchronous I/O classes:
    * DNS lookup
    * File I/O
    * Filesystem APIs like `mkdir` and `stat`
    * Abstract asynchronous stream interface
    * In-process pipes
    * TCP sockets, with or without TLS
    * A TCP listener (no TLS support yet)
    * URL parser
    * HTTP client
    * HTTP server (_very_ basic so far)
    * WebSocket client and server
    * BLIP RPC protocol (optional; under [separate license][BLIP].)
    
* Core classes & APIs:
    * General-purpose `Error` and `Result<T>` types
    * Logging, a thin wrapper around [spdlog][SPDLOG]

* Cross-Platform:
    * macOS (builds and passes tests)
      * iOS? ("It's still Darwin…")
    * Linux (builds and passes test)
      * Android? ("It's still Linux…")
    * Windows (sometimes builds; not yet tested; help wanted!)
    * Would very much like to support some embedded platforms like ESP32 (help wanted!)

## Example

```c++
// Simple HTTP client request:
HTTPConnection client("https://example.com");
HTTPRequest req;
HTTPResponse resp = co_await req.sendRequest(req);

cout << int(resp.status()) << " " << resp.statusMessage() << endl;

for (auto &header : resp.headers())
    cout << header.first << " = " << header.second << endl;

ConstBuf body;
do {
    body = co_await resp.readNoCopy();
    cout << string_view(body);
} while (body.len > 0);
cout << endl;
```

## Status: ☠️EXPERIMENTAL☠️

This is new code, under heavy development! So far, it builds with Clang (Xcode 15) on macOS, GCC 12 on Ubuntu, and Visual Studio 17 2022 on Windows.

The tests have been run on macOS and Ubuntu. Test coverage is very limited.

APIs are still in flux. Things get refactored a lot.

## Building It

> **Important:** Make sure you checked out the submodules! 
> `git submodule update --init --recursive`

### Prerequisites:

- CMake
- Clang 15, Xcode 15, or GCC 12
- zlib (aka libz)

#### on macOS:

- Install Xcode 15 or later, or at least the command-line tools.
- Install CMake; this is most easily done with [HomeBrew](https://brew.sh), by running `brew install cmake`

#### on Ubuntu Linux

    sudo apt-get install g++ cmake cmake-data zlib1g-dev

### Building With CMake

    make
    make test

The library is `libCrouton`, in either the `build_cmake/debug/` or `build_cmake/release/` directory.

### Building With Xcode

**Before first building with Xcode**, you must use CMake to build libuv and mbedTLS:

    make xcode_deps

You only need to do this on initial setup, or after those submodules are updated.

Then:
- open crouton.xcodeproj
- Select the `Tests` scheme and Run. 
- To locate the binaries, choose Product > Show Build Folder In Finder


## License(s)

* Crouton itself is licensed under the [Apache 2](./LICENSE) license.
  * The files in the subdirectory `src/io/blip`, however, are under the [Business Software License][BSL] since they are adapted from existing BSL-licensed code. **These source files are optional** and are by default not compiled into the Crouton library. See their [README][BLIP] for details and the full license.
  * The source file `src/io/URL.cc` contains some code adapted from tlsuv, which is Apache 2 licensed.
* Of the third party code in `vendor/`:
  * Catch2 is licensed under the Boost Software License (but is only linked into the tests)
  * libuv is licensed under the MIT license.
  * llhttp is licensed under the MIT license.
  * mbedtls is licensed under the Apache 2 license.
  * spdlog is licensed under the MIT license.

## Credits

- Crouton code by Jens Alfke ([@snej][SNEJ])
- Initial inspiration, coroutine knowledge, starting code: [Simon Tatham's brilliant tutorial][TUTORIAL]. [Lewis Baker's blog posts][BAKER] have also been helpful.
- Event loops, I/O, networking: [libuv][LIBUV]
- TLS engine: [mbedTLS][MBEDTLS]
- HTTP parser: [llhttp][LLHTTP]
- Logging: [spdlog][SPDLOG]
- Unit tests: [Catch2][CATCH2]
- Extra inspiration and URL parser: [tlsuv][TLSUV]

[SNEJ]: https://github.com/snej
[TUTORIAL]: https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/
[LIBUV]: https://libuv.org
[TLSUV]: https://openziti.io/tlsuv/
[LLHTTP]: https://github.com/nodejs/llhttp
[MBEDTLS]: https://github.com/Mbed-TLS/mbedtls
[SPDLOG]: https://github.com/gabime/spdlog
[CATCH2]: https://github.com/catchorg/Catch2
[BAKER]: https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform
[BSL]: src/io/blip/licences/BSL.txt
[BLIP]: src/io/blip/README.md
