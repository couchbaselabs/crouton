# Results

A `Result<T>` is a “tagged union” type, much like `std::variant`, that can hold either an instance of `T` or an `Error`. The `Error` might be `noerror`, so there are actually three states: a value, an error, or nothing.

> `Result<void>` is a special case whose value doesn’t have any data, but it does distinguish between having a value and not having one.

## 1. Creating Results

- `Result`’s default constructor creates an empty instance holding nothing, as you’d expect.
- `Result` can be constructed or converted (implicitly) from a `T` value.
- `Result` can be constructed or converted (implicitly) from an `Error`, which stores that error.

You can also use `=` to assign a `T` or `Error` to an existing `Result` variable, or call `set(_)` which does the same thing.

## 2. Checking Results

Result is normally used as the return type of a function that might fail. In the case of Generator, the empty case also indicates end-of-data. Normally you’ll want to check it for errors and then either handle the error or extract the value.

Three methods to check the type of a Result are:

* `ok()` returns true if there is a value.
* `isError()` returns true if there’s an error.
* `empty()` returns true if there’s neither a value nor an error.

Another way to test a Result is just to treat it like a boolean, e.g. `if (result) ...` This returns true if there’s a value, false if it’s empty, and _throws an exception_ if there’s an error. This allows you to treat a Result much like a `std::optional` if you want to keep using C++ exceptions for error handling.

To handle errors explicitly without exceptions, do something like this:

```c++
Result<string> r = ....;
if (r.isError())
    handleError(r.error());
else if (r.ok())
    cout << "The answer is " << r.value() << endl;
else
    cout << "Answer hazy; try again later." << endl;
```

As shown above, you get the `T` value from a `Result<T>` with `r.value()`. As a shortcut you can also treat the Result as a pointer: `*r` is a reference to the value, and `r->foo` is equivalent to `r.value().foo`.

> Getting the value from a `Result` that doesn’t have one throws an exception: its error if there is one, else `CroutonError::EmptyResult`.

## 3. Results and Futures

A `Future<T>` coroutine can fail, either by throwing an exception or `co_return`ing an `Error`. This makes the value of the Future an `Error`.

```c++
Future<string> readFile(string path) {
    ...
    co_return UVError(UV_ENOENT);
}
```

Normally, when you `co_await` a `Future<T>`, you get a value of type `T`.  But on error, the Future’s error will be thrown via `Error::raise`.

If you don’t want this, you can wrap the Future in `NoThrow` before awaiting it; then you get a `Result` that you can inspect for errors.

```c++
Future<string> f = readFile("/bogus");
Result<string> result = co_await NoThrow(f);
```


