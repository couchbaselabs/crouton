#  Crouton

Crouton is a C++20 coroutine runtime library that provides some general purpose utilities, as well as cross-platform event loops, I/O and networking based on the [libuv][LIBUV], [mbedTLS][MBEDTLS] and [llhttp][LLHTTP] libraries. (On Apple platforms it can also use the system Network.framework.)

A **Coroutine** is _a function that can return partway through, and then later be resumed where it left off._ Knuth wrote about them in the 1960s and they remained a curiosity for a long time, but they've since become widely used under the hood of the "async / await" concurrency model used in languages like JavaScript, C#, Rust, Nim, and Swift. Crouton brings this to C++.

Async/await gives you concurrency without the pitfalls of multithreading. You can write code in a linear fashion, "blocking" on slow operations like I/O (see the [example](#Example) below), but the thread doesn't really block: instead your (coroutine) function is suspended and some other function that's finished blocking can resume. When the I/O completes, your function is resumed transparently at the next opportunity.

How is that better than threads? It's safer and easier to reason about. The only places where concurrency happens are well-marked by the `co_await` and `co_yield` keywords. You don't need mutexes or atomic variables, and there are far fewer opportunities for race conditions or deadlocks. (There are performance benefits too: no expensive context switches, less stack usage.)

## Features

* General purpose utilities:
    * Useful coroutine base classes
    * `Generator`, an iterator implementation based on `co_yield`-ing values
    * `Future`, an asynchronous promise type
    * `CoMutex`, a utility that allows only one coroutine to enter at a time

* Event loops:
    * `Scheduler`, which manages multiple active coroutines on a thread
        * Round-robin scheduling of multiple active coroutines
        * Suspending a coroutine, then waking it when it's ready
        * Scheduling a function to run on the next event-loop iteration, even on a different thread
        * Scheduling a function to run on a background thread-pool
    * `Task`, an independently-running coroutine that can `co_yield` to give time to others
    * `Timer`, repeating or one-shot
    * `Actor`, a class whose coroutine methods are queued, never running concurrently
    
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
    
* Cross-Platform:
    * macOS (builds and passes tests)
      * iOS? ("It's still Darwin…")
    * Linux (builds; not yet tested)
      * Android? ("It's still Linux…")
    * Windows ("it oughta work…" but not yet built or tested)
    
## Status: ☠️EXPERIMENTAL☠️

This is very new code! So far, it builds with Clang (Xcode 14) on macOS, GCC 12 on Ubuntu, and Visual Studio 17 2022 on Windows.

The tests have only been run on macOS yet. Test coverage is very limited.

APIs are still in flux.

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

## Building It

> **Important:** Make sure you checked out the submodules! 
> `git submodule update --init --recursive`

### Prerequisites:

- CMake
- GCC, Clang or Xcode

#### on macOS:

- Install Xcode 14 or later, or at least the command-line tools.
- Install CMake; this is most easily done with [HomeBrew](https://brew.sh), by running `brew install cmake`

#### on Linux

    sudo apt-get install g++ cmake cmake-data`

### Building With CMake

    make all
    make test

The library is `libCrouton`, in the `build_cmake/debug/` or `build_cmake/release/` directory.

### Building With Xcode

**Before first building with Xcode**, you must use CMake to build libuv and mbedTLS:

    make xcode_deps

You only need to do this on initial setup, and after those submodules are updated.

Then:
- open crouton.xcodeproj
- Select the `Tests` scheme and Run. 
- To locate the binaries, choose Product > Show Build Folder In Finder


## Credits

- Crouton code by Jens Alfke ([@snej][SNEJ])
- Initial inspiration, coroutine knowledge, starting code: [Simon Tatham's brilliant tutorial][TUTORIAL].
- Event loops, I/O, networking: [libuv][LIBUV] (MIT license)
- TLS engine: [mbedTLS][MBEDTLS] (Apache license)
- HTTP parser: [llhttp][LLHTTP] (MIT license)
- Extra inspiration and URL parser: [tlsuv][TLSUV]

[SNEJ]: https://github.com/snej
[TUTORIAL]: https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/
[LIBUV]: https://libuv.org
[TLSUV]: https://openziti.io/tlsuv/
[LLHTTP]: https://github.com/nodejs/llhttp
[MBEDTLS]: https://github.com/Mbed-TLS/mbedtls
