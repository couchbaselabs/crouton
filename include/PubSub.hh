//
// PubSub.hh
//
// 
//

#pragma once
#include "Awaitable.hh"
#include "Generator.hh"
#include "Queue.hh"
#include "Task.hh"

namespace crouton::ps {

    /** Type-erasing wrapper around any Series implementation. */
    template <typename T>
    class AnySeries final : public Series<T> {
    public:
        template <std::derived_from<Series<T>> Impl>
        explicit AnySeries(Impl&& impl)
        :_impl(std::make_unique<Impl>(std::forward<Impl>(impl)))
        { }

        bool await_ready() override                         {return _impl->await_ready();}
        coro_handle await_suspend(coro_handle cur) override {return _impl->await_suspend(cur);}
        Result<T> await_resume() override                   {return _impl->await_resume();}

    private:
        std::unique_ptr<Series<T>> _impl;
    };



    /** Abstract base class of objects that provides `Series` of `T` items to `Subscriber`s.
        Publishers are always managed with `std::shared_ptr` because their lifespans are
        unpredictable.*/
    template <typename T>
    class Publisher : public std::enable_shared_from_this<Publisher<T>> {
    public:
        virtual ~Publisher() = default;

        /// Creates a Series of items for a Subscriber to read.
        virtual AnySeries<T> generate() = 0;
    };



    /** Abstract base class of objects that read a series of `T` items from a `Publisher`. */
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

        /// Starts the Subscriber: it first calls `generate` on its Publisher to get the Series,
        /// then starts an async Task to await items.
        virtual void start() {
            if (!_series) {
                assert(_publisher);
                _series.emplace(_publisher->generate());
                _task.emplace(run(std::move(_series).value()));
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
        virtual Task run(AnySeries<T> gen) {
            while (true) {
                Result<T> result = AWAIT gen;
                if (result.ok()) {
                    handle(std::move(result).value());
                } else {
                    handleEnd(result.error());
                    break;
                }
            }
        }

        /// Abstract method that handles an item received from the Publisher.
        /// @warning  You must override either this method or `run`.
        virtual Future<void> handle(T)          {return CroutonError::LogicError;}

        /// Handles the final Error/noerror item from the publisher.
        /// Default implementation sets the `error` property; make sure to call through.
        virtual void handleEnd(Error err)       {_error = err;}

    private:
        Subscriber(Subscriber const&) = delete;
        Subscriber& operator=(Subscriber const&) = delete;

        std::shared_ptr<Publisher<T>> _publisher;
        std::optional<AnySeries<T>> _series;
        std::optional<Task> _task;
        Error               _error;
    };



    /** Abstract base class implementing both Publisher and Subscriber.
        These are used as intermediate links in data-flow chains. */
    template <typename In, typename Out = In>
    class Connector : public Subscriber<In>, public Publisher<Out> {
    };


    
    namespace detail {
        template <typename T> T test_pub_type(const Publisher<T>*);
        template <typename> void test_pub_type(const void*);
        template <typename T> T test_sub_type(const Subscriber<T>*);
        template <typename> void test_sub_type(const void*);
    }
    /// If T is a subclass of `Publisher<P>`, `pub_type<T>` evaluates to P.
    template <typename T> using pub_type = decltype(detail::test_pub_type((const T*)nullptr));
    /// If T is a subclass of `Subscriber<P>`, `sub_type<T>` evaluates to P.
    template <typename T> using sub_type = decltype(detail::test_sub_type((const T*)nullptr));


    /// `|` can be used to chain together Publishers and Subscribers.
    /// The left side must be a `shared_ptr` of a Publisher type.
    /// The right side can be a `shared_ptr` of a Subscriber, or directly a Subscriber.
    /// The Subscriber will be subscribed to the Publisher, and returned.
    template <typename P, typename S>  requires(std::is_same_v<pub_type<P>,sub_type<S>>)
    auto operator| (shared_ptr<P> pub, shared_ptr<S> sub) {
        sub->subscribeTo(std::move(pub));
        return sub;
    }

    template <typename P, typename S>  requires(std::is_same_v<pub_type<P>,sub_type<S>>)
    S& operator| (shared_ptr<P> pub, S &sub) {
        sub.subscribeTo(std::move(pub));
        return sub;
    }

    template <typename P, typename S>  requires(std::is_same_v<pub_type<P>,sub_type<S>>)
    S&& operator| (shared_ptr<P> pub, S &&sub) {
        sub.subscribeTo(std::move(pub));
        return std::forward<S>(sub);
    }


#pragma mark - UTILITY IMPLEMENTATIONS


    /** Publisher that publishes a canned list of items and optionally an error.
        Each Subscriber will receive the full list of items.*/
    template <typename T>
    class Emitter : public Publisher<T> {
    public:
        explicit Emitter(std::vector<T> &&items)           :_items(std::move(items)) { }
        explicit Emitter(std::initializer_list<T> items)   :_items(items) { }

        /// Sets an error to return at the end.
        void endWithError(Error err)                        {_error = err;}

        AnySeries<T> generate() override                    {return AnySeries<T>(_generate());}

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



    /** Subscriber that stores items into a vector. */
    template <typename T>
    class Collector : public Subscriber<T> {
    public:
        using Subscriber<T>::Subscriber;
        std::vector<T> const& items()                      {return _items;}
    protected:
        Future<void> handle(T result) override {
            _items.emplace_back(std::move(result));
            return Future<void>{};
        }
    private:
        std::vector<T> _items;
    };



    /** A trivial subclass of AsyncQueue that implements Publisher.
        Call `push` to enqueue items which will then be delivered to the Subscriber.
        @warning  This currently only supports a single Subscriber. */
    template <typename T>
    class QueuePublisher : public Publisher<T>, public AsyncQueue<T> {
    public:
        using super = AsyncQueue<T>;
        using super::AsyncQueue;

        AnySeries<T> generate() override    {return AnySeries<T>(super::generate());}
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

        AnySeries<T> generate() override    {return AnySeries<T>(super::generate());}
    };



    /** A pub-sub connector that can buffer a fixed number of items. */
    template <typename T>
    class Buffer : public Connector<T> {
    public:
        explicit Buffer(size_t queueSize)   :_queue(queueSize) { }

        AnySeries<T> generate() override    {
            this->start();
            return AnySeries<T>(_queue.generate());
        }

    protected:
        // (predicate is exposed by subclass Filter)
        using Predicate = std::function<bool(T const&)>;
        Buffer(size_t queueSize, Predicate p)   :_queue(queueSize), _predicate(std::move(p)) { }

    private:
        Task run(AnySeries<T> gen) override {
            bool eof, closed = false;
            do {
                Result<T> item = AWAIT gen;
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



    /** A pub-sub connector that passes on only the items that satisfy a predicate function. */
    template <typename T>
    class Filter : public Buffer<T> {
    public:
        explicit Filter(std::function<bool(T const&)> p) :Buffer<T>(1, std::move(p)) { }
    };



    /** A pub-sub connector that reads items, transforms them through a caller-defined function,
        and re-publishes them. */
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
            return AnySeries<Out>(_queue.generate());
        }

    protected:
        Task run(AnySeries<In> gen) override {
            bool eof, closed;
            do {
                Result<In> item = AWAIT gen;
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

}
