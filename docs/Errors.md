# Errors

`Error` is a value type representing an error. It’s lighter-weight than an exception and can be used in code that doesn’t enable C++ exceptions.

Lots of APIs represent errors as integers or C `enum`s, with each value denoting a different type of error. Usually 0 is reserved to mean “no error”.

Crouton’s Error class embraces this. In fact it can hold any type of error enum and keep track of the type as well as the numeric value. So it could hold `ECONNREFUSED` or `errSecAuthFailed`, and remember that the first is a POSIX error and the second is a mac/iOS `OSStatus`.

An Error object is only 4 bytes large and is ‘plain old data’ with no constructor or destructor, which makes it very cheap to pass around.

## 1. Error Domain Enums

An error enum that Error understands is called an Error Domain. It needs some additional metadata that tells Error its name and the names of its error codes. This is done by specializing the template `ErrorDomainInfo`. You probably won’t need to do this, because there are a number of error enums provided already:

* `CroutonError` — Errors in core Crouton APIs
* `CppError` — Represents standard C++ exception classes like `bad_alloc` and `logic_error`
* `io::UVError` — libuv error codes, most of which mimic POSIX errors
* `io::http::Status` — HTTP status codes like 404, 500,…
* `io::ws::CloseCode` — WebSocket close codes
* `io::apple::POSIXError` — POSIX errors from Apple’s Network.framework
* `io::apple::DNSError` — Error codes from Apple’s `dns_sd` API 
* `io::apple::TLSError` — `OSStatus` error codes from Apple’s Security/Keychain APIs
* `io::mbed::MbedError` — mbedTLS errors

> Two other requirements for error enums are: 0 represents “no error”, and error codes are in the range ±131072.

## 2. Creating Errors

Error’s constructor can take an error domain enum value, a C++ `std::exception` reference, or a `std::exception_ptr`. In the latter two cases it will do the best it can to map the exception class to a `CppError` enum value.

The constant `noerror` is an Error value with a code of 0. Any Error constructed from an error domain enum whose value is 0 is equivalent to `noerror`.

## 3. Interpreting Errors

First off, you can check whether an `Error` *is* an error by checking `if (err != noerror)`, or more simply, `if (err)`.

You can get the error code as a plain integer with `code()`, and get the name of the domain with `domain()`.

If you want to check whether the error is of a specific domain enum, use `is<Domain>`, for example `if (err.is<io::UVError>())`. To get the error code as an enum of that type, use `err.as<io::UVError>()`. This returns the original `UVError` enum value, or a 0 value if the error isn’t of that domain.

To check for a specific error enum, you can simply check equality: `if (err == CroutonError::Cancelled)`.

To get an error’s description/message, call `description()` or `brief()`. The latter just returns the domain name and numeric code.

## 4. Errors and Exceptions

You can catch a C++ exception and capture it as an Error. In fact, the built-in coroutine classes do this if you throw an uncaught exception from a coroutine function. And you can throw an Error as an exception too, which can happen to the caller who `co_await`ed the failed coroutine.

Capturing an exception looks like this:

```c++
try {
    ...
} catch (...) {
    Error err(std::current_exception());
}
```

Throwing an `Error` looks like this:

```c++
Error err = ...;
err.raise();       // always throws an exception even if err==noerror
err.raise_if();    // only throws if err!=noerror
```

A thrown `Error` takes the form of an instance of `Exception`, which is a simple class that inherits from both `std::exception` and `Error`.

When an exception is converted into an `Error`, the constructor recognizes if the exception is of type `Exception` and just pulls the `Error` out of it, preserving fidelity.
