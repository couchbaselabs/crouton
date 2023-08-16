//
// Generator.hh
//
// Copyright © 2021 Jens Alfke. All rights reserved.
//

#include "Coroutine.hh"
#include <iterator>
#include <optional>

// Adapted from Simon Tatham's brilliant tutorial
// <https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/>
// Specifically, I started from "co_demo.exception.cpp".


namespace tendril::coro {
    template <typename T> class Future;
    template <typename T> class Waiter;

    template <class U> class InputAwaiter;

    template <typename T> class GeneratorImpl;

    /** The public type representing a running coroutine instance.
        Returned by coroutine functions. */
    template <typename T>
    class Generator : public CoroutineHandle<GeneratorImpl<T>> {
    public:
        using super = CoroutineHandle<GeneratorImpl<T>>;

        /// Blocks until the Generator next yields a value, then returns it.
        /// Returns `nullopt` when the Generator is complete (its coroutine has exited.)
        /// \warning Do not call this from a coroutine! Instead, `co_await` the Generator.
        std::optional<T> next()                 {return super::impl().next();}

        // Generator is iterable, but only once.
        class iterator;
        iterator begin()                        {return iterator(*this);}
        std::default_sentinel_t end()           {return std::default_sentinel_t{};}

    private:
        friend class GeneratorImpl<T>;
        template <typename U> friend class Generator;
        template <typename U> friend class InputAwaiter;
        
        explicit Generator(super::handle_type handle)  :super(handle) {}
    };



    // Iterator interface to a Generator's values; not for use in coroutines, sadly
    template <typename T>
    class Generator<T>::iterator {
    public:
        iterator& operator++()      {_value = _gen.next(); return *this;}
        T const& operator*() const  {return _value.value();}
        T& operator*()              {return _value.value();}
        friend bool operator== (iterator const& i, std::default_sentinel_t)   {return !i._value;}

    private:
        friend class Generator;
        explicit iterator(Generator& gen)   :_gen(gen), _value(gen.next()) { }

        Generator&       _gen;
        std::optional<T> _value;
    };


#pragma mark - IMPLEMENTATION GUNK:

    
    // `Impl` is the intermediary between the public Generator class and the internals of
    // the C++ coroutine mechanism.
    // It doesn't formally have a base class, but must implement a number of "magic" methods.
    template <typename T>
    class GeneratorImpl : public CoroutineImpl<Generator<T>, GeneratorImpl<T>> {
    public:
        using super = CoroutineImpl<Generator<T>, GeneratorImpl<T>>;

        GeneratorImpl() = default;

        void clear() {
            super::clear();
            _yielded_value = std::nullopt;
        }

        // Implementation of the public Generator's next() method. Called by non-coroutine code.
        std::optional<T> next() {
            clear();
            if (auto h = super::handle(); !h.done())
                h.resume();   // Resume coroutine fn, unless it already completed.
            return yielded_value();
        }

        // Returns the value yielded by the coroutine function after it's run.
        std::optional<T> yielded_value() {
            super::rethrow();
            return std::move(_yielded_value);
        }


        //---- C++ coroutine internal API:

        // Invoked once when the coroutine function is called, to create its return value.
        // At this point the function hasn't done anything yet.
        Generator<T> get_return_object()            {return Generator<T>(super::handle());}

        // Invoked by the coroutine's `co_yield`. Captures the value and transfers control.
        template <std::convertible_to<T> From>
        Yielder yield_value(From&& value) {
            _yielded_value = std::forward<From>(value);
            auto resumer = _consumer;
            _consumer = std::noop_coroutine();
            return Yielder{resumer};
        }

        // Invoked when the coroutine fn returns without a result, implicitly or via co_return.
        // If co_return is to take a parameter, implement `return_value` instead:
        // `void return_value(XXX value) { ... }`
        void return_void() { }

    private:
        template <class U> friend class Generator;
        template <class U> friend class InputAwaiter;

        /// Tells me which coroutine should resume after I co_yield the next value.
        void returnControlTo(std::coroutine_handle<> consumer) {_consumer = consumer;}

        std::optional<T>        _yielded_value;                    // Latest value yielded
        std::coroutine_handle<> _consumer = std::noop_coroutine(); // Coroutine awaiting my value
    };

    

    // Manages control flow during `co_await` on a "producer" Generator<U> object.
    // Created by GeneratorImpl::await_transform().
    template <typename T>
    class InputAwaiter {
    public:
        using ProducerImpl = GeneratorImpl<T>;
        explicit InputAwaiter(ProducerImpl& p)      :_producer(p) {}

        // Invoked during the `co_await` call to ask if the current coroutine should keep going.
        bool await_ready()                          {return false;}

        // Invoked when the coroutine suspends during `co_await`.
        // Returns the handle of the coroutine that should take control next.
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> suspending) {
            _producer.clear();
            _producer.returnControlTo(suspending);
            return _producer.handle();
        }
        
        // Invoked when the coroutine resumes, to get the value to return from `co_await`.
        std::optional<T> await_resume()             {return _producer.yielded_value();}

    private:
        ProducerImpl& _producer;    // The Impl of the Generator I'm waiting on
    };

    template <typename T>
    InputAwaiter<T> operator co_await(Generator<T>& gen) {
        return InputAwaiter<T>{gen.impl()};
    }

}
