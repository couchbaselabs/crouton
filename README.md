#  Crouton

Crouton is a C++20 coroutine runtime library that provides some general purpose utilities, as well as cross-platform event loops, I/O and networking based on the [libuv][LIBUV], mbedTLS[MBEDTLS] and [tlsuv][TLSUV] libraries. (On Apple platforms it can also use the system Network.framework.)

## Features

* General purpose utilities:
    * Useful coroutine base classes
    * `Future`, an asynchronous promise type
    * `Generator`, an iterator implementation based on `co_yield`-ing values

* Event loops:
    * `Scheduler`, which manages multiple active coroutines on a thread
        * Scheduling a function to run on the next event-loop iteration, even on a different thread
        * Scheduling a function to run on a background thread-pool
    * `Task`, an independently-running coroutine that can `co_yield` to give time to others
    * `Timer`, repeating or one-shot
    * `Actor`, a class whose coroutine methods are queued, never running concurrently
    
* Asynchronous I/O classes:
    * DNS lookup
    * File I/O
    * In-process pipes (streams)
    * TCP sockets, with or without TLS
    * HTTP client
    * WebSocket client
    * A TCP server/listener (without TLS support so far)
    * URL parser
    
## Status

This is very new code! So far, it builds with Clang on macOS and passes some basic tests.

## Example

```c++
HTTPClient client("https://example.com");
HTTPRequest req(client, "GET", "/");
HTTPResponse resp = co_await req.response();

cout << int(resp.status) << " " << resp.statusMessage << endl;

auto headers = resp.headers();
while (auto header = co_await headers)
    cout << header->first << " = " << header->second << endl;

string body;
do {
    body = co_await resp.readBody();
    cout << body;
} while (!body.empty());
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

**Before first building with Xcode**, you must use CMake to build some libraries:

    make xcode_deps

You only need to do this on initial setup, and after any of the submodules are updated.

Then:
- open crouton.xcodeproj
- Select the `Tests` scheme and Run. 
- To locate the binaries, choose Product > Show Build Folder In Finder


## Credits

- Crouton code by Jens Alfke ([@snej][SNEJ])
- Initial inspiration, coroutine knowledge, starting code: [Simon Tatham's brilliant tutorial][TUTORIAL].
- Event loops, I/O, networking: [libuv][LIBUV] (MIT license)
- TLS, HTTP, WebSockets: [tlsuv][TLSUV] (MIT license)
  - TLS engine: [mbedTLS][MBEDTLS] (Apache license)
  - HTTP parser: [llhttp][LLHTTP] (MIT license)

[SNEJ]: https://github.com/snej
[TUTORIAL]: https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/
[LIBUV]: https://libuv.org
[TLSUV]: https://openziti.io/tlsuv/
[LLHTTP]: https://github.com/nodejs/llhttp
[MBEDTLS]: https://github.com/Mbed-TLS/mbedtls
