#  Crouton

Crouton is a C++20 coroutine runtime library that provides some general purpose utilities, as well as cross-platform event loops, I/O and networking based on the [libuv][LIBUV] and [tlsuv][TLSUV] libraries.

## Features

* General purpose utilities:
    * Useful coroutine base classes
    * `Future`, an asynchronous promise type
    * `Generator`, an iterator implementation based on `co_yield`-ing values

* Event loops:
    * `Scheduler`, which manages multiple active coroutines on a thread
    * `Task`, an independently-running coroutine that can `co_yield` to give time to others
    * `Timer`, repeating or one-shot
    * Scheduling a function to run on the next event-loop iteration, even on a different thread
    * Scheduling a function to run on a background thread-pool
    
* Asynchronous I/O classes:
    * DNS lookup
    * File I/O
    * In-process pipes (streams)
    * TCP sockets, with or without TLS
    * HTTP client
    * WebSocket client
    * A TCP server/listener (without TLS support so far)
    * URL parser
    
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

## Status

This is very new code! It builds with Clang/Xcode on macOS and passes some basic tests.

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
