#  Crouton

Crouton is a C++20 coroutine runtime library that provides some general purpose utilities, as well as a cross-platform I/O implementation based on the libuv and tlsuv libraries.

## Features

* General purpose utilities:
    * Useful coroutine base classes
    * `Future`, an asynchronous promise type
    * `Generator`, an iterator implementation based on `co_yield`-ing values

* libuv-based event loops:
    * `Scheduler`, which schedules multiple coroutines on a thread
    * `Task`, an independently-running coroutine that can `co_yield` to give time to others
    * `Timer`, repeating or one-shot
    * Scheduling a function to run on the next event-loop iteration, even on a different thread
    * Scheduling a function to run on a background thread-pool
    
* Asynchronous I/O classes:
    * DNS lookup
    * File I/O
    * In-process pipes (streams)
    * TCP sockets, with or without TLS
    * WebSockets
    * A TCP server/listener (without TLS support so far)
    * URL parser

## Status

This is very new code! It builds with Clang on macOS and passes some basic tests.

## Credits

Crouton code by Jens Alfke.

Initial inspiration, coroutine knowledge, starting code: [Simon Tatham's brilliant tutorial][TUTORIAL].

Event loops, I/O, networking: [libuv][LIBUV] (MIT license)

TLS and WebSockets for libuv: [tlsuv][TLSUV] (MIT license)

Underlying TLS engine: mbedTLS

[TUTORIAL]: https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/
[LIBUV]: https://libuv.org
[TLSUV]: https://openziti.io/tlsuv/
