# Publishers and Subscribers

Crouton includes a small _functional reactive programming_ framework based on the notions of **Publishers** and **Subscribers**.

* `Publisher<T>` is an interface describing an object that can, on demand, create `ISeries<T>` objects.
* `ISeries<T>` is an interface describing an awaitable object producing type `Result<T>`, with the contract that it will produce zero or more instances of `T`, ending with either an `Error` or `noerror`. (`Generator` is a specific implementation of `ISeries`.)
* A `Subscriber<T>` is an object that gets an `ISeries<T>` from a `Publisher<T>` and asynchronously consumes its items.
* A `Connector` is both a `Publisher` and a `Subscriber`: it consumes items and in response generates items, which may be of a different type.

These interfaces are modular units that can be combined to produce data flows with a Publisher at the start, zero or more Connectors, ending in a Subscriber. For example:

> `WebSocket` ➜ `Filter` ➜ `Transform` ➜ `FileWriter`

This connects to a WebSocket server, picks out matching WebSocket messages, transforms them (perhaps into human-readable strings) and then writes them to a file. The test case "Stream Publisher" in `test_pubsub.cc` is very similar to this.

## Premade Publishers & Subscribers

There are a number of implementations of Publisher, Subscriber and Connector that you can plug together.

* **Publishers**:
  * `Emitter` is constructed with a list of items which it stores in a `std::vector`. When a Subscriber connects, the Emitter sends it all of the items.
* **Connectors**:
  * `BaseConnector` simply routes items unchanged from its publisher to its subscriber (it only supports one subscriber.) It’s intended for subclassing.
  * `Buffer` also routes items unchanged, but it has a fixed-capacity internal queue of items. If the queue fills up, it stops reading from the publisher until the subscriber catches up. This is useful for flow control.
  * `Filter` takes a boolean-valued function and calls it on every item; it passes along only items for which the function returns true.
  * `Transformer` takes a function that converts each item into a different item, which could be a different type. The converted items are passed to the subscriber.
  * `Timeout` passes through items from the publisher, except that if the first item takes longer than a given interval to arrive, it sends the subscriber a `CroutonError::Timeout` error and stops.
* **Subscribers**:
  * `Collector` is the opposite of `Emitter`: it just collects the items into a `vector` which can be examined afterwards.
  * `CollectorFn` instead takes a function and calls it on every item it receives.

## Creating Workflows

The easiest way to connect publishers and subscribers is with the `|` operator, as though you were in a shell. Here’s an example taken directly from `test_pubsub.cc`:

```c++
auto collect = AnyPublisher<string,io::FileStream>("README.md")
             | LineSplitter{}
             | Contains("Crouton")
             | Collector<string>{};
collect.start();
```

## Publisher

`Publisher<T>` is a simple interface: it has one method, `publish()`, that returns  a `unique_ptr` to an `ISeries<T>`.  This method is called by the `Subscriber<T>` connected to the Publisher, to start the action.

To implement a Publisher, just subclass `Publisher<T>` and override the `publish()` method.

Alternatively, you can create a Publisher from an `AsyncQueue` or `IStream` — or anything else that has a `generate` method returning an `ISeries` — by using the `AnyPublisher` template. For example, the class `AnyPublisher<string,FileStream>`  is a subclass of `FileStream` that also implements `Publisher<string>`.

## Subscriber

`Subscriber<T>` is a bit more complex because it does more of the work. 

First, it’s given a shared reference to a Publisher, either in its constructor or by a call to the `subscribeTo()` method.

Then its `start()` method is called; this calls the Publisher to get an `ISeries` and passes that series to the `run()` method.

The `run()` method is a `Task` coroutine, so it can run indefinitely. It’s a loop that awaits an item from the series and processes it, and stops once it gets an EOF or Error.

You can implement a Publisher by subclassing `Publisher<T>` and either

* overriding `run` to implement the whole loop yourself, or
* overriding `handle(T)`, and optionally `handleEnd(Error)`, which receive individual items.

## Connector

The abstract class `Connector<In,Out>` simply subclasses both `Subscriber<In>` and `Publisher<Out>`.

A more useful base class is `BaseConnector`, which uses a `SeriesProducer` to output a series; its `produce(Result<Out>)` method sends the next result to the subscriber. You can extend this class by overriding `run`.
