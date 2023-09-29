//
// PubSub.hh
//
// 
//

#pragma once
#include "Awaitable.hh"
#include "Defer.hh"
#include "Generator.hh"
#include "Queue.hh"
#include "Select.hh"
#include "Task.hh"

#include <functional>
#include <optional>
#include <vector>

namespace crouton::ps {

    /** Type-erasing wrapper around any Series implementation. */
    template <typename T>
    class AnySeries final : public ISeries<T> {
    public:
        /// Constructs an `AnySeries<T>` by moving a `Series<T>` rvalue.
        template <std::derived_from<ISeries<T>> Impl>
        AnySeries(Impl&& impl)
        :_impl(std::make_unique<Impl>(std::forward<Impl>(impl)))
        { }

        bool await_ready() override                         {return _impl->await_ready();}
        coro_handle await_suspend(coro_handle cur) override {return _impl->await_suspend(cur);}
        Result<T> await_resume() override                   {return _impl->await_resume();}
        void onReady(OnReadyFn fn) override                 {return _impl->onReady(std::move(fn));}

    private:
        std::unique_ptr<ISeries<T>> _impl;
        // Note: it's not possible to implement this with std::any, bc Series is not copyable
    };



    /** A `Publisher<T>` asynchronously provides `Series` of `T` items to `Subscriber`s.
        @note  Publishers are always managed with `std::shared_ptr` because their lifespans are
               unpredictable (each active Subscriber has a reference.) */
    template <typename T>
    class Publisher : public std::enable_shared_from_this<Publisher<T>> {
    public:
        virtual ~Publisher() = default;

        /// Creates a Series of items for a Subscriber to read.
        /// @note  If this is called a second time, when the first Series has already produced
        ///        items, the second Series may or may not include the already-produced items.
        /// @warning  Some implementations don't support multiple subscribers. Check the docs.
        virtual AnySeries<T> generate() = 0;
    };



    /** A `Subscriber<T>` asynchronously receives a series of `T` items from a `Publisher`.
        Many Subscriber implementations are also Publishers (see `Connector`), allowing chains
        or pipelines to be created. */
    template <typename T>
    class Subscriber {
    public:
        Subscriber() = default;
        explicit Subscriber(std::shared_ptr<Publisher<T>> pub)  {subscribeTo(pub);}

        /// Connects the subscriber to a Series created by the Publisher.
        /// @warning  Must be called exactly once.
        virtual void subscribeTo(std::shared_ptr<Publisher<T>> pub) {
            assert(!_publisher);
            _publisher = std::move(pub);
        }

        /// The Publisher I'm subscribed to.
        std::shared_ptr<Publisher<T>> publisher() const  {return _publisher;}

        /// Starts the Subscriber: it first calls `generate` on its Publisher to get the Series,
        /// then starts an async Task to await items.
        virtual void start() {
            if (!_task) {
                assert(_publisher);
                _task.emplace(receive(_publisher->generate()));
            }
        }

        /// True after the Subscriber has received the final EOF/error from the Publisher.
        bool done() const                       {return _task && !_task->alive();}

        /// The final Error, or noerror, from the Publisher.
        /// @note  Before the Subscriber is done, this returns `noerror`.
        Error error() const                     {return _error;}

        Subscriber(Subscriber&& s)              {*this = std::move(s);}
        Subscriber& operator=(Subscriber&& s)   {assert(!_task); _publisher = std::move(s._publisher); return *this;}
        virtual ~Subscriber()                   {assert(!_task || done());}

    protected:
        /// Coroutine method that's the lifecycle of the Subscriber.
        /// It awaits items from the Generator and passes them to the `handle` method,
        /// until it receives the final empty or error item.
        /// You can override this if you want more control over the lifecycle.
        /// @warning  You must override either this method or `handle(T)`.
        /// @warning  If you override, you are responsible for calling `handleEnd` when finishing.
        virtual Task receive(AnySeries<T> series) {
            while (true) {
                Result<T> result = AWAIT series;
                if (result.ok()) {
                    handle(std::move(result).value());
                } else {
                    handleEnd(result.error());
                    break;
                }
            }
        }

        /// Abstract method that handles an item received from the Publisher.
        /// @warning  You must override either this method or `receive`.
        virtual Future<void> handle(T)          {return CroutonError::LogicError;}

        /// Handles the final Error/noerror item from the publisher.
        /// Default implementation sets the `error` property; make sure to call through.
        virtual void handleEnd(Error err)       {_error = err;}

    private:
        Subscriber(Subscriber const&) = delete;
        Subscriber& operator=(Subscriber const&) = delete;

        std::shared_ptr<Publisher<T>>   _publisher; // My Publisher
        std::optional<Task>             _task;      // The `receive` coroutine that reads _series
        Error                           _error;     // Error received from _series
    };



    /** Simple base class implementing both Publisher and Subscriber.
        These are used as intermediate links in data-flow chains.

        @note  Subclasses' `generate()` implementations should call `this->start()`. */
    template <typename In, typename Out = In>
    class Connector : public Subscriber<In>, public Publisher<Out> {
    public:
        using Subscriber<In>::Subscriber;
    };


#pragma mark - CHAINING


    namespace detail {
        template <typename T> T test_pub_type(const Publisher<T>*);
        template <typename> void test_pub_type(const void*);
        template <typename T> T test_sub_type(const Subscriber<T>*);
        template <typename> void test_sub_type(const void*);

        template <typename T> T test_shared_pub_type(const std::shared_ptr<Publisher<T>>*);
        template <typename> void test_shared_pub_type(const void*);
    }
    /// Template utility: If `P` is a Publisher, `pub_type<P>` is its item type.
    /// In other words, if P is a subclass of `Publisher<T>`, `pub_type<P>` evaluates to `T`.
    template <typename P> using pub_type = decltype(detail::test_pub_type((const P*)nullptr));
    /// Template utility: If `P` is a Subscriber, `sub_type<P>` is its item type.
    template <typename S> using sub_type = decltype(detail::test_sub_type((const S*)nullptr));


    /// `|` can be used to chain together Publishers and Subscribers.
    /// The left side must be a Publisher type [see note] or a `shared_ptr` to one.
    /// The right side must be a Subscriber type, or a `shared_ptr` to one.
    /// The Subscriber will be subscribed to the Publisher, and returned.
    ///
    /// @note If the Publisher is not already a `shared_ptr` it will be moved into one,
    ///       since Publishers are always shared.
    ///
    /// Examples:
    /// ```
    /// auto collector = Emitter<int>(...) | Filter<int>(...) | Collector<int>{};
    /// auto stream = make_shared<AStream>(...);
    /// auto collector = stream | Filter<string>(...) | Collector<string>{};
    /// ```
    template <typename P, typename S, typename PP = std::remove_reference_t<P>, typename SS = std::remove_reference_t<S>>  requires(std::is_same_v<pub_type<PP>,sub_type<SS>>)
    [[nodiscard]] S operator| (P&& pub, S&& sub) {
        auto sharedP = std::make_shared<PP>(std::move(pub));
        sub.subscribeTo(std::move(sharedP));
        return sub;
    }

    template <typename P, typename S, typename PP = std::remove_reference_t<P>, typename SS = std::remove_reference_t<S>>  requires(std::is_same_v<pub_type<PP>,sub_type<SS>>)
    S operator| (std::shared_ptr<P> pub, S&& sub) {
        sub.subscribeTo(std::move(pub));
        return sub;
    }

    template <typename P, typename S, typename PP = std::remove_reference_t<P>, typename SS = std::remove_reference_t<S>>  requires(std::is_same_v<pub_type<PP>,sub_type<SS>>)
    auto operator| (std::shared_ptr<P> pub, std::shared_ptr<S> sub) {
        sub->subscribeTo(std::move(pub));
        return sub;
    }


#pragma mark - UTILITY SUBSCRIBERS


    /** Subscriber that stores items into a vector. */
    template <typename T>
    class Collector : public Subscriber<T> {
    public:
        using Subscriber<T>::Subscriber;
        std::vector<T> const& items()                       {return _items;}
    protected:
        Future<void> handle(T result) override {
            _items.emplace_back(std::move(result));
            return Future<void>{};
        }
    private:
        std::vector<T> _items;
    };


#pragma mark - UTILITY PUBLISHERS


    /** Publisher that publishes a canned list of items and optionally an error.
        Each Subscriber will receive the full list of items.*/
    template <typename T>
    class Emitter : public Publisher<T> {
    public:
        explicit Emitter(std::vector<T> &&items)           :_items(std::move(items)) { }
        explicit Emitter(std::initializer_list<T> items)   :_items(items) { }

        /// Sets an error to return at the end.
        void endWithError(Error err)                        {_error = err;}

        AnySeries<T> generate() override                    {return _generate();}

    private:
       Generator<T> _generate() {
            for (T& item : _items)
                YIELD item;
            if (_error)
                YIELD _error;
        }
        std::vector<T>  _items;
        Error           _error;
    };



    /** A trivial subclass of AsyncQueue that implements Publisher;
        useful for creating a Publisher from a non-PubSub source of events.
        Call `push` to enqueue items which will then be delivered to Subscribers.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class QueuePublisher : public Publisher<T>, public AsyncQueue<T> {
    public:
        using super = AsyncQueue<T>;
        using super::AsyncQueue;

        AnySeries<T> generate() override                    {return super::generate();}
    };


    /** A trivial subclass of BoundedAsyncQueue that implements Publisher.
        This is like QueuePublisher except it has a limited capacity.
        Call `asyncPush` to enqueue items; if the queue is full it waits until there's room.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class BoundedQueuePublisher : public Publisher<T>, public BoundedAsyncQueue<T> {
    public:
        using super = BoundedAsyncQueue<T>;
        using super::BoundedAsyncQueue;

        AnySeries<T> generate() override                    {return super::generate();}
    };



#pragma mark - UTILITY CONNECTORS


    /** Minimal concrete implementation of Connector, that simply propagates items.
        Supports multiple subscribers. */
    template <typename T>
    class BaseConnector : public Connector<T> {
    public:
        using Connector<T>::Connector;

        AnySeries<T> generate() override {
            this->start();
            if (_subscriberCount == 0)
                _readyForItem.notify();
            ++_subscriberCount;
            return _generate();
        }

    protected:
        Task receive(AnySeries<T> series) override {
            do {
                // Await next item from upstream Publisher:
                Result<T> nextItem = AWAIT series;

                // Wait until downstream Subscribers have finished with the current `_item`:
                AWAIT _readyForItem;

                // Update `_item` and `_eof`
                _eof = !nextItem.ok();
                _item = std::move(nextItem);

                // Wake up subscribers (`_generate`):
                assert(_usingItemCount == 0);
                _usingItemCount = _subscriberCount;
                _itemAvailable.notifyAll();
            } while (!_eof);
        }

        Generator<T> _generate() {
            DEFER {--_subscriberCount;};
            do {
                while (_usingItemCount == 0) {
                    AWAIT _itemAvailable;
                }
                if (!_item.empty())
                    YIELD _item;
                assert(_usingItemCount > 0);
                if (--_usingItemCount == 0)
                    _readyForItem.notify();
            } while (!_eof);
        }

    private:
        Result<T>     _item;                // Item produced by `receive` & consumed by `generate`
        CoCondition   _itemAvailable;       // Notifies generators there's a new item to send
        Blocker<void> _readyForItem;        // Notifies `receive` it can update `_item`
        int           _subscriberCount = 0; // Number of subscribers (_generate coroutines)
        int           _usingItemCount = 0;  // Number of subscribers still sending `_item`
        bool          _eof = false;         // True once an EOF item is received
    };



    /** A Connector that can buffer a fixed number of items in an internal queue.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class Buffer : public Connector<T> {
    public:
        explicit Buffer(size_t queueSize)   :_queue(queueSize) { }

        AnySeries<T> generate() override    {this->start(); return _queue.generate();}

    protected:
        // (exposed by subclass Filter)
        using Predicate = std::function<bool(T const&)>;
        Buffer(size_t queueSize, Predicate p)   :_queue(queueSize), _predicate(std::move(p)) { }

    private:
        Task receive(AnySeries<T> series) override {
            bool eof, closed = false;
            do {
                Result<T> item = AWAIT series;
                eof = !item.ok();
                if (eof)
                    this->handleEnd(item.error());
                if (eof || !_predicate || _predicate(item.value()))
                    closed = ! AWAIT _queue.asyncPush(std::move(item));
            } while (!eof && !closed);
            _queue.closeWhenEmpty();
        }

        BoundedAsyncQueue<T> _queue;
        Predicate            _predicate;
    };



    /** A Connector that passes on only the items that satisfy a predicate function.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class Filter : public Buffer<T> {
    public:
        explicit Filter(std::function<bool(T const&)> p) :Buffer<T>(1, std::move(p)) { }
    };



    /** A Connector that reads items, transforms them through a function, and re-publishes them.
        @note  The function may end the series early by returning an Error or `noerror`.
               But it may not extend the series by returning a T when it gets an EOF.
        @warning  This currently only supports a single Subscriber. */
    template <typename In, typename Out>
    class Transformer : public Connector<In,Out> {
    public:
        using XformFn = std::function<Result<Out>(Result<In>)>;

        explicit Transformer(XformFn xform, size_t queueSize = 1)
        :_queue(queueSize)
        ,_xform(std::move(xform))
        { }

        AnySeries<Out> generate() override {
            this->start();
            return _queue.generate();
        }

    protected:
        Task receive(AnySeries<In> series) override {
            bool eof, closed;
            do {
                Result<In> item = AWAIT series;
                bool inEof = !item.ok();
                Result<Out> out = _xform(std::move(item));
                eof = !out.ok();
                assert(!inEof || eof);  // EOF input has to produce EOF output
                if (eof)
                    this->handleEnd(out.error());
                closed = ! AWAIT _queue.asyncPush(std::move(out));
            } while (!eof && !closed);
            _queue.closeWhenEmpty();
        }

    private:
        BoundedAsyncQueue<Out> _queue;
        XformFn _xform;
    };


    template <typename T>
    class Timeout : public Connector<T> {
    public:
        explicit Timeout(double secs)   :_timeout(secs) { }
    private:
        double _timeout;
    };

}
