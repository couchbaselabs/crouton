# Introduction To Crouton

A **coroutine** is a function that can be interrupted or _suspended_ at specific points, and then later _resumed_ as though nothing had happened. Coroutines enable concurrent programming on a single thread, with no need for mutexes or atomic operations. A lot of languages have coroutines now, often under the name "[async/await](https://en.wikipedia.org/wiki/Async%2Fawait)": C#, F#, Haskell, JavaScript, Kotlin, Nim, Python, Rust, Zig... and C++, as of C++20.

The [C++20 documentation](https://en.cppreference.com/w/cpp/language/coroutines), and books and articles about C+20, make coroutines sound very complex and difficult. They aren’t. The problem is that the C++20 standard library only includes the lowest-level infrastructure for implementing coroutines, which is a level below what most people want to use. It's like a bare motherboard and a pile of chips, not a working PC. The documentation is mostly aimed at people who want to build coroutine libraries (like me!), not at people who just want to _use_ coroutines.

What’s needed is a premade C++ coroutine library with higher-level abstractions and utilities. That’s what Crouton is. But it's more...

One domain where coroutines are extremely useful is I/O. Coroutines let you treat asynchronous I/O calls as though they were normal blocking ones, making them a lot easier to use, without adding the overhead and complexity of multi-threading. Crouton provides a whole set of classes for file I/O, TCP sockets, HTTP clients and servers, and WebSockets. Under the hood these are built on the cross-platform library `libuv`, which is the native layer at the core of node.js.

Another great use of coroutines is for producer/consumer queues. A pair of coroutines can interact via a queue, where a producer creates data values and pushes them into the queue, while a consumer pulls values from the same queue and operates on them. This approach lends itself to modular components that can be plugged together into complex data flows. There's a whole domain of "Functional Reactive Programming" based on this; examples are React and Apple's Combine. Crouton provides `Publisher` and `Subscriber` interfaces and a set of building blocks based on them.

But back to basics...

## 1. What’s a coroutine?

In general, a coroutine is a function that can be interrupted or _suspended_ at specific points, and then later _resumed_ as though nothing had happened. A single thread can switch between running different coroutines. Coroutines are useful for implementing:

* **Generators**, a type of iterator implemented as a loop that ‘yields’ multiple values to its caller. (The Python language uses these a lot.)
* **Async/await**, a style of concurrent programming where an `async` function returns a "promise" or "future" result before it actually finishes running; callers get that object immediately but have to `await` the actual result.
* **Fibers**, cooperatively-scheduled independent tasks running on a single thread.

It’s interruption at _specific points_ that makes coroutines different from pre-emptively scheduled threads. A coroutine can only be suspended when it explicitly performs an **await** or a **yield** operation. You can write single-threaded concurrent code with coroutines without worrying about low-level race conditions. The only time other code can mess up a function’s state is when that function suspends.
 
> ⚠️ A coroutine can of course be interrupted by another thread, just like any other function! And if it's accessing state that's also visible to code on other threads, that can definitely create race conditions requiring the use of mutexes or atomics. The advantage of coroutines is that you can write concurrent code where everything that accesses mutable state runs on a single thread. You can still use multiple threads, but in safer and more limited ways that don't involve sharing mutable state.

Another difference is that a coroutine's **yield** operation can produce a value. This value gets passed to the coroutine (or normal function) that invoked it. This is very powerful because it allows coroutines to return multiple results, like an iterator, and without losing state. A coroutine producing multiple values can do so by yielding inside a `for` or `while` loop, instead of having to turn its control flow inside-out the way a typical C++ iterator does.

### Example

Here's an example of a `Generator`, a Crouton coroutine class that yields multiple values:

```c++
static Generator<double> fibonacci() {
    double a = 1, b = 1;
    co_yield a;            // <-- suspends coroutine and produces value for caller
    while (true) {
        co_yield b;
        tie(a, b) = pair{b, a + b};
    }
}
```

And here’s one way to call it:

```c++
Generator<double> fib = fibonacci();
Result<double> n;
while ((n = co_await fib) {   // <-- resumes coroutine, then returns its next value
    cout << n.value() << ", ";
    if (n.value() > 1000) break;
}
```

## 2. What’s a C++ coroutine?

### Three magic keywords

In C++20 or later, a coroutine is any function — or method or lambda — that uses any of the three magic keywords `co_await`, `co_yield` and `co_return`. When the compiler sees one of these keywords, it restructures the function under the hood. Each of these is a prefix operator that's followed by an expression (but in `co_return` the expression is optional, as with regular `return`.)

**`co_await X`** is used to interrupt/block the coroutine until `X` produces a result. `X` needs to be of an _awaitable_ type such as a `Future`, `Generator`, `Blocker`... That value gets to decide whether and how long to interrupt the coroutine, and what value to return as the result of the `co_await` expression.

**`co_yield X`** is used to _produce_ a value `X` that will be received by a caller. It usually transfers control from the coroutine back to its caller. `co_yield` may, but usually doesn’t, return a value when the coroutine resumes.

**`co_return X`**, or just **`co_return`**, is the coroutine equivalent of `return`, and _must_ be used instead of `return`. In a coroutine that produces a single value like a `Future`, the value returned becomes the result.

> Because these keywords have such an effect on the flow of control, and because they represent points where other code may take over and change state, Crouton defines some all-caps macros equivalent to them: `AWAIT`, `YIELD` and `RETURN`. These stand out more in the code. Using them is of course optional.

### Coroutine objects

The *specific* behavior of those three “`co-`” keywords depends on what type of coroutine the function is implementing. That in turn depends on the function’s return type. The return type maps to library code that defines an internal class that actually handles the `co_await`, `co_yield` and `co_return` calls. That class has a lot of leeway in how to handle them, which makes coroutines very flexible.

These coroutine implementation classes are difficult to write — that’s what all the complex C++20 documentation is about. Fortunately Crouton provides several of them for you -- see [[Coroutine Types]].

So when using Crouton, when you write a coroutine function that returns `Generator<T>`, you're writing a generator, with those behaviors. Or if your function returns `Future<T>`, you're writing a future, which has different behaviors. This object is the external manifestation of the coroutine, what the caller sees.

> In this documentation we’ll use capitalization to distinguish between a coroutine function and the object representing it: so for example a `future` is a coroutine _function_, while a `Future` is the _object_ it returns.

The return type of a coroutine function is usually not the same as the type of value it `co_return`s! The relationship between those types is up to the implementation class. While a coroutine is running, the caller of the coroutine already has the object representing it, the `Future`, `Generator`, etc. When the coroutine function returns a value, that value is _produced by_ the object; when the caller awaits a value, that's what it'll get.

## Flow of control

Here's an example of the flow of control that may help clarify things. Let's go back to the above example of a Generator that yields the Fibonacci sequence. Here's what happens when the calling code runs:

1. The outer code calls the function `fibonacci()`.
2. Since `fibonacci()` is a coroutine, its entry point doesn't actually start the body of the function yet. Instead it creates an internal object (of class `GeneratorImpl<double>`) to hold the coroutine state. That object creates a `Generator<double>` object, which is returned to the caller.
3. The caller calls `co_await fib`. This suspends the caller (which must be a coroutine itself!) and transfers control to the fibonacci coroutine.
4. The fibonacci coroutine function itself now begins to run.
5. The fibonacci coroutine function calls `co_yield a`. This saves the argument (1), suspends the coroutine, and transfers control back to the caller.
6. The caller resumes, and its `co_await` expression produces the value `1`.
7. The caller prints the value and jumps back to the top of the loop.
8. The fibonacci coroutine resumes. Its `co_yield` call returns, and it enters its loop and calls `co_yield b`.
9. The caller resumes, and its `co_await` expression produces the value `1`.
10. The caller prints the value and jumps back to the top of the loop....

This could go on forever -- the Fibonacci sequence is infinite after all -- but in this case the caller's loop stops when it gets a value greater than 1000. After that, the generator won't be called again. What happens to it? When the Generator object goes out of scope, it's destructed like any other object. This in turn destructs the `GeneratorImpl` and the internal coroutine state.

When this happens, the coroutine function just abruptly exits while suspended in the `co_yield` call. It's as though `co_yield` itself made the function return or threw an uncatchable exception (though that isn't really what happens.)

## What Next?

* See the [types of coroutines available](Coroutine Types.md)
* See the [types of objects coroutines can `co_await`](Awaitable Types.md)
* [`Result`](Results.md) and [`Error`](Errors.md) are two ubiquitous lower-level data types.

