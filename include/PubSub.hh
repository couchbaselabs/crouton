//
// PubSub.hh
//
// 
//

#pragma once
#include "Awaitable.hh"
#include "Defer.hh"
#include "Generator.hh"
#include "Producer.hh"
#include "Queue.hh"
#include "Select.hh"
#include "Task.hh"

#include <functional>
#include <optional>
#include <vector>

namespace crouton::ps {

    template <typename T> using SeriesRef = std::unique_ptr<ISeries<T>>;

    template <typename T, std::derived_from<ISeries<T>> Impl>
    SeriesRef<T> mkseries(Impl &&impl)       {return std::make_unique<Impl>(std::forward<Impl>(impl));}
    template <typename T>
    SeriesRef<T> mkseries(SeriesRef<T>&& ref) {return ref;}


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
        virtual SeriesRef<T> publish() = 0;
    };



    /** Concept matching classes with a `generate()` method returning a subclass of `ISeries<T>`.
        Examples are `AsyncQueue`, and `IStream`'s many implementations. */
    template <class Gen, typename T>
    concept GeneratorFactory = requires (Gen gen) {
        { gen.generate() } -> std::derived_from<ISeries<T>>;
    };


    /** Utility class that subclases anything with a `generate()` method returning an `ISeries`
        -- such as AsyncQueue or IStream -- and makes it implement Publisher.*/
    template <typename T, GeneratorFactory<T> Gen>
    class AnyPublisher : public Gen, public Publisher<T> {
    public:
        using Gen::Gen;
        SeriesRef<T> publish() override                     {return mkseries<T>(Gen::generate());}
    };

    

    /** A `Subscriber<T>` asynchronously receives a series of `T` items from a `Publisher`.
        Many Subscriber implementations are also Publishers (see `Connector`), allowing chains
        or pipelines to be created. */
    template <typename T>
    class Subscriber {
    public:
        /// Constructs an unconnected Subscriber; you must call `subscribeTo` afterwards.
        Subscriber() = default;

        /// Constructs a Subscriber connected to a Publisher.
        explicit Subscriber(std::shared_ptr<Publisher<T>> pub)  {subscribeTo(std::move(pub));}

        explicit Subscriber(SeriesRef<T> series)                {_series = std::move(series);}

        /// Connects the subscriber to a Publisher.
        virtual void subscribeTo(std::shared_ptr<Publisher<T>> pub) {
            assert(!_publisher);
            assert(!_series);
            _publisher = std::move(pub);
        }

        /// The Publisher it's subscribed to.
        std::shared_ptr<Publisher<T>> publisher() const         {return _publisher;}

        /// Starts the Subscriber: it first calls `generate` on its Publisher to get the Series,
        /// then starts an async Task to await items.
        /// @note  You only have to call this on the last Subscriber in a series.
        virtual void start() {
            if (!_task) {
                SeriesRef<T> series = std::move(_series);
                if (!series) {
                    assert(_publisher);
                    series = _publisher->publish();
                }
                _task.emplace(run(std::move(series)));
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
        /// until it receives the final empty or error item, then passes that to `handleEnd`.
        /// 
        /// You can override this if you want more control over the lifecycle.
        /// @warning  You must override either this method or `handle(T)`.
        /// @warning  If you override, you are responsible for calling `handleEnd` when finishing.
        virtual Task run(SeriesRef<T> series) {
            while (true) {
                Result<T> result = AWAIT *series;
                if (result.ok()) {
                    AWAIT handle(std::move(result).value());
                } else {
                    handleEnd(result.error());
                    break;
                }
            }
        }

        /// Abstract method that handles an item received from the Publisher.
        /// @warning  You must override either this method or `run`.
        virtualASYNC<void> handle(T)            {return CroutonError::Unimplemented;}

        /// Handles the final Error/noerror item from the publisher.
        /// Default implementation sets the `error` property; make sure to call through.
        virtual void handleEnd(Error err)       {_error = err;}

    private:
        Subscriber(Subscriber const&) = delete;
        Subscriber& operator=(Subscriber const&) = delete;

        std::shared_ptr<Publisher<T>>   _publisher; // My Publisher, if given
        SeriesRef<T>                    _series;    // My Series, if given
        std::optional<Task>             _task;      // The `receive` coroutine that reads series
        Error                           _error;     // Error received from the publisher
    };



    /** Simple base class implementing both Publisher and Subscriber.
        These are used as intermediate links in data-flow chains.

        @note  Subclasses' `publish()` implementations should call `this->start()`. */
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
        auto sharedP = std::make_shared<PP>(std::forward<P>(pub));
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


    /** Subscriber that calls a function on the values it receives. */
    template <typename T>
    class CollectorFn : public Subscriber<T> {
    public:
        using Fn = std::function<Future<void>(Result<T>)>;
        explicit CollectorFn(Fn fn) :_fn(std::move(fn)) { }
    protected:
        virtual Task run(SeriesRef<T> series) {
            bool ok;
            do {
                Result<T> result = AWAIT *series;
                ok = result.ok();
                AWAIT fn(std::move(result));
            } while (ok);
        }
    private:
        Fn _fn;
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

        SeriesRef<T> publish() override                    {return mkseries<T>(_publish());}

    private:
       Generator<T> _publish() {
            for (T& item : _items)
                YIELD item;
            if (_error)
                YIELD _error;
        }
        std::vector<T>  _items;
        Error           _error;
    };



#pragma mark - UTILITY CONNECTORS


    /** Minimal concrete implementation of Connector, that simply propagates items.
        @warning  This currently only supports a single Subscriber. */
    template <typename In, typename Out = In>
    class BaseConnector : public Connector<In,Out> {
    public:
        using Connector<In,Out>::Connector;

        SeriesRef<Out> publish() override {
            this->start();
            return _producer.make_consumer();
        }

    protected:
        Task run(SeriesRef<In> series) override {
            Error error;
            bool eof;
            do {
                Result<In> nextItem = AWAIT *series;
                eof = !nextItem.ok();
                if (eof)
                    error = nextItem.error();
                if (! AWAIT produce(std::move(nextItem)) && !error) {
                    error = CroutonError::Cancelled;
                    eof = true;
                }
            } while (!eof);
            this->handleEnd(error);
        }

        /// Sends the item to subscriber. Must be awaited; if result is false, stop.
        [[nodiscard]] SeriesProducer<Out>::AwaitProduce produce(Result<Out> nextItem) {
            return _producer.produce(std::move(nextItem));
        }

    private:
        SeriesProducer<Out> _producer;
        bool _eof = false;
    };



    /** A Connector that can buffer a fixed number of items in an internal queue.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class Buffer : public Connector<T> {
    public:
        explicit Buffer(size_t queueSize)   :_queue(queueSize) { }

        SeriesRef<T> publish() override    {this->start(); return mkseries<T>(_queue.generate());}

    protected:
        // (exposed by subclass Filter)
        virtual bool filter(T const& item)  {return true;}

    private:
        Task run(SeriesRef<T> series) override {
            bool eof, closed = false;
            do {
                Result<T> item = AWAIT *series;
                eof = !item.ok();
                if (eof)
                    this->handleEnd(item.error());
                if (eof || filter(item.value()))
                    closed = ! AWAIT _queue.asyncPush(std::move(item));
            } while (!eof && !closed);
            _queue.closeWhenEmpty();
        }

        BoundedAsyncQueue<T> _queue;
    };



    /** A Connector that passes on only the items that satisfy a predicate function.
        There are two ways to provide the function:
        1. Pass it to the constructor as a `std::function`.
        2. Subclass, and override the `filter` method.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class Filter : public Buffer<T> {
    public:
        using Predicate = std::function<bool(T const&)>;

        explicit Filter(Predicate p = {})
        :Buffer<T>(1)
        ,_predicate(std::move(p))
        { }

    protected:
        bool filter(T const& item) override {return _predicate(item);}
    private:
        Predicate            _predicate;
    };



    /** A Connector that reads items, transforms them through a function, and re-publishes them.
        There are two ways to provide the function:
        1. Pass it to the constructor as a `std::function`.
        2. Subclass, and override either of the `transform` methods.
        @note  The function may end the series early by returning an Error or `noerror`.
               But it may not extend the series by returning a T when it gets an EOF.
        @warning  This currently only supports a single Subscriber. */
    template <typename In, typename Out>
    class Transformer : public Connector<In,Out> {
    public:
        using XformFn = std::function<Result<Out>(Result<In>)>;

        explicit Transformer(size_t queueSize = 1)
        :_queue(queueSize)
        { }

        explicit Transformer(XformFn xform, size_t queueSize = 1)
        :_queue(queueSize)
        ,_xform(std::move(xform))
        { }

        SeriesRef<Out> publish() override {
            this->start();
            return mkseries<Out>(_queue.generate());
        }

    protected:
        virtual Result<Out> transform(Result<In> item) {
            if (_xform)
                return _xform(std::move(item));
            else if (item.ok())
                return transform(std::move(item).value());
            else
                return item.error();
        }

        virtual Out transform(In item) {
            Error(CroutonError::Unimplemented).raise();
        }

    private:
        Task run(SeriesRef<In> series) override {
            bool eof, closed;
            do {
                Result<In> item = AWAIT *series;
                bool inEof = !item.ok();
                Result<Out> out = transform(std::move(item));
                eof = !out.ok();
                assert(!inEof || eof);  // EOF input has to produce EOF output
                if (eof)
                    this->handleEnd(out.error());
                closed = ! AWAIT _queue.asyncPush(std::move(out));
            } while (!eof && !closed);
            _queue.closeWhenEmpty();
        }

        BoundedAsyncQueue<Out> _queue;
        XformFn _xform;
    };

    

    /** A Connector that produces an error if its upstream Publisher doesn't produce its first
        item within a given time. */
    template <typename T>
    class Timeout : public BaseConnector<T> {
    public:
        explicit Timeout(double secs)   :_timeout(secs) { }
    protected:
        Task run(SeriesRef<T> series) override {
            {
                // Wait for a first item, or the timeout:
                Future<void> timeout = Timer::sleep(_timeout);
                Select select {&timeout, &series};
                select.enable();
                if ((AWAIT select) == 0) {
                    this->produce(CroutonError::Timeout);
                    RETURN;
                }
            }
            BaseConnector<T>::run(std::move(series));
            RETURN;
        }
    private:
        double _timeout;
    };

}
