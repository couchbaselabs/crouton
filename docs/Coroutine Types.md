# Types Of Coroutines In Crouton

## 1. `Future<T>`

A **Future** is a wrapper around a value that may not be available yet. These are sometimes called promises. Most languages have them; there’s even an unrelated `std::future` in C++11 that is not coroutine-aware.

When a coroutine returns type `Future<T>`, it’s expected to `co_return` a value of type `T`, or an `Error`. Along the way it can `co_await` as much as it wants. The first time it awaits, control will return to the calling function, which gets the `Future<T>` object even though there’s no value in it yet.

A future-returning coroutine can also throw an exception; if so, the exception will be wrapped up as an `Error` and treated as the Future’s result.

The most common thing to do with a `Future<T>` is to `co_await` it; this blocks the current coroutine until the Future’s result is available, and returns it as a value of type `T`. It’s also possible the Future’s result is an error; if so, the error will be thrown as an exception. (There are ways to avoid this and examine the result as an Error instead.)

### The `ASYNC` macro

Future is such a common return type, there's a macro to highlight it:
```c++
    ASYNC<string> readString();
```

`ASYNC` is short for `[[nodiscard]] Future`. The annotation makes it an error to ignore the return value; otherwise it's easy to forget to `co_await` it.

### Creating Futures without coroutines

It’s possible for non-coroutine code to create, return and even await Futures. In fact this is the main way to bridge between coroutine and non-coroutine functions.

> Note: Returning `Future<T>` doesn't make a function a coroutine. It's only a coroutine if it uses `co_await`, `co_yield` or `co_return`.

Most simply, you can create a `Future` that already has a value or an error simply by constructing it with one, like `Future<int>(17)` or `Future<string>(CroutonError::Unimplemented)`. As these are implicit constructors, if the function returns a Future you can just return the value/error:

```c++
    Future<int> answer()         {return 6 * 7;}
    Future<string> fancyThing()  {return CroutonError::Unimplemented;}
```

The interesting case is if you _don't_ have the value yet. In that case you create a `FutureProvider` first and hang onto it (it’s a reference, a `shared_ptr`.) You construct a `Future` from it and return that. Later, you call `setResult` or `setError` on the provider to give the Future a value.

```c++ 
Future<double> longCalculation(double n) {
    FutureProvider<double> provider = Future<double>::provider();
    longCalculationWithCallback(n, [provider] (double answer) {
        provider->setResult(answer);	// When result arrives, store it in the Future
    });
    return Future<double>(provider);	// For now, return the empty Future
}
```

### Awaiting a Future

What about the other direction: you call a function that returns a Future, but you’re not in a coroutine and can’t use `co_await`?

In that case you usually use a callback. `Future::then()` takes a lambda that will be called when the Future’s value is available, and passed the value.

```c++
Future<double> answerF = longCalculation(123.456);
answerF.then([=](Result<double> answer) { cout << answer.value() << endl; });
```

A `then` callback can even return a new value, which will become the value of a new Future:

```c++
Future<string> longCalculationAsString(double n) {
	Future<string> answer = longCalculation(n).then([=](Result<double> answer) {
    	return std::to_string(answer.value());
    });
    return answer;
});
```

In the above example, what happens is:

1. `longCalculationAsString` calls `longCalculation`, which returns an `empty Future<double>`.
2. `longCalculationAsString` returns an empty `Future<string>`.
3. `longCalculation` finishes, and the lambda ls called.
4. The lambda’s return value is stored in the `Future<string>`.

## 2. `Generator<T>`

A `Generator` is a coroutine that produces multiple results, rather like an iterator. A coroutine whose return type is `Generator<T>` is expected to call `co_yield` with a value of type `T` zero or more times, then `co_return` void. It may also `co_await` along the way.

The caller receives a `Generator<T>` object even before the coroutine has started running. The coroutine remains suspended until the caller `co_await`s the Generator; then the caller in turn is suspended and control passes to the coroutine. When the coroutine calls `co_yield` or `co_return`, the caller wakes up and receives a value of type `Result<T>` as the result of the `co_await` expression. `Result` is similar to `std::variant`: it contains either a value of type `T`, or an `Error`, or the special empty value `noerror` which in this case means the generator finished.

Here’s a simple example of a generator:

```c++
static Generator<double> fibonacci() {
    double a = 1, b = 1;
    co_yield a;
    while (true) {
        co_yield b;
        tie(a, b) = pair{b, a + b};
    }
}
```

And here’s an example of how to call it:

```c++
Generator<double> fib = fibonacci();
Result<double> n;
while ((n = co_await fib) {   // <-- resumes coroutine, then returns its next value
    cout << n.value() << ", ";
    if (n.value() > 1000) break;
}
```

Note that the generator is an infinite loop: it yields a literally infinite series of Fibonacci numbers! But generators are *lazy*, so in reality it only produces values as often as it’s called. The example caller stops once the numbers exceed 1000.

## 3. `Task`

A `Task` is a sort of cooperative thread. A coroutine that returns `Task` is a function that can run as long as it wants and isn’t expected to produce or return anything directly. The caller doesn’t need to hang onto the returned Task object, although it may do so if it wants to check on whether the coroutine is still running.

> ⚠️ `Task` isn’t appropriate for CPU-intensive code, or for code that calls non-coroutine APIs that might block for a long time. The task coroutine still runs on the current thread, and coroutines are cooperatively scheduled, so any coroutine that doesn’t call `co_await` or `co_yield` in a timely manner is hogging its thread and preventing anything else from running on it. Crouton has other mechanisms for running code on a background thread.

