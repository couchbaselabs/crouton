# Types Of Awaitable Objects In Crouton

An *awaitable* type is one that you can use as the argument to `co_await`. Not everything is awaitable! An expression like `co_await 17` is meaningless. The behavior of `co_await x` is determined by a set of special methods implemented by `x`'s class. These are complex, but Crouton provides a set of useful awaitable types.

## 1. `Future<T>`

`co_await Future<T>` waits until the Future has a result, then returns it; or if the Future’s result is an error, it throws it as an exception.

If the Future already has a result, `co_await` doesn't block at all, just returns the result.

## 2. `Generator<T>`

`co_await Generator<T>` transfers control to the generator coroutine. When that function `co_yield`s another value , control returns to the caller and `co_await` returns the new value wrapped in a `Result`. Or if the generator coroutine `co_return`s, `co_await` returns an empty `Result`.

## 2. `Blocker<T>`

Blocker is a control-flow utility similar to a condition variable: a simple way to block a coroutine until something happens, and pass a value back. It’s very useful in adapting existing callback-based asynchronous APIs.

It’s used like this:

1. Construct a `Blocker<T>` with no arguments.
2. Trigger some task that will eventually call `notify(value)`on the blocker object (even from a different thread.)
3. `co_await` the blocker. This suspends the coroutine until `notify` is called, then returns the value passed to `notify`.

For example:

```c++
Blocker<int> blocker;
some_async_api(x, y, [&](int arg) { blocker.notify(arg); });
int result = co_await blocker;
```

That’s all it takes to turn a callback-based API into a straightforward ‘blocking’ one. (Of course it's not really blocking. While this coroutine is suspended, other coroutines on the same thread continue running.)

## 3. CoCondition

CoCondition is similar to Blocker, but simultaneously more and less powerful. On the plus side:

* multiple coroutines can `co_await` a `CoCondition` at once; the `notifyOne` method will wake up the first one that blocked, and the `notifyAll` method will wake them all up.

But on the other hand:

* it doesn’t pass a result value to the waiting coroutines.
* it’s not thread-safe.

## 4. `AsyncQueue<T>`

`AsyncQueue<T>` is a FIFO queue of values of type `T`, similar to `std::deque`. What’s special is that you can pop values from it asynchronously with `co_await`. If the queue is empty, this suspends until a value is pushed into it.

You don't await the queue itself, you get a `Generator` from it and await that:

```c++ 
AsyncQueue<string> queue;
...
    
Generator<string> g = queue.generate();
optional<string> next;
while ((next = co_await g)) {
    cout << next.value() << endl;
}
```

When does that loop ever end? When you close the AsyncQueue. Depending on how it’s closed, the generator may immediately end, or end once the last value has been pulled. It might even produce an `Error`.

## 5. `BoundedAsyncQueue<T>`

This is a subclass of AsyncQueue that has a pre-set capacity. If it’s at capacity and you try to push a value, the `push()` call will return `false`.  

More usefully, you can call `asyncPush()` — this method is a coroutine that waits until a value is pulled out, then pushes the new value. It of course returns a `Future` that resolves when the push completes.

## 6. `Select`

Select exists to solve a tricky problem: what if you have two different awaitables, and you only want to wait until the *first* one is ready?

A Select object is constructed with two or more awaitable values. Next you tell it which ones to enable (watch). Then `co_await`ing the Select will block until one of the awaitables is ready, and then return its index. You can then `co_await` that awaitable without blocking.

Here’s an example of how to wait for a background task but time out after 30 seconds:

```c++
Future<double> computation = verySlowBackgroundTask();
Future<void> timeout = Timer::sleep(30.0);

Select select {&computation, &timeout};
select.enable();
switch( co_await select ) {
    case 0: {
        double answer = co_await computation;
        cout << "The answer is " << answer << endl;
        break;
    }
    case 1:
        cout << "Sorry, it's taking too long." << endl;
        break;
}
```
