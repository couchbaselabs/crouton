//
// Generator.hh
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include "Coroutine.hh"
#include "Result.hh"
#include "Scheduler.hh"
#include "Select.hh"
#include <exception>
#include <iterator>
#include <utility>

namespace crouton {
    template <typename T> class GeneratorImpl;


    using OnReadyFn = std::function<void()>;


    /** Public face of a coroutine that produces values by calling `co_yield`.

        Awaiting a Generator returns the next value, wrapped in a `Result`,
        or an empty Result when the Generator finishes, or an error Result on failure.

        A Generator can also be iterated synchronously via its begin/end methods,
        or with a `for(:)` loop.*/
    template <typename T>
    class Generator : public Coroutine<GeneratorImpl<T>>, public ISelectable {
    public:
        Generator(Generator&&) = default;

        ~Generator() {
            if (auto h = this->handle()) {
                if (!h.done())
                    this->impl().stop();
                h.destroy();
            }
        }

        /// Blocks until the Generator next yields a value, then returns it.
        /// Returns `nullopt` when the Generator is complete (its coroutine has exited.)
        /// \warning Do not call this from a coroutine! Instead, `co_await` the Generator.
        Result<T> next()                            {return this->impl().next();}

        // Generator is iterable, but only once.
        class iterator;
        iterator begin()                            {return iterator(*this);}
        std::default_sentinel_t end()               {return std::default_sentinel_t{};}

        void onReady(OnReadyFn fn) override         {this->impl().onReady(std::move(fn));}

        //---- Generator is awaitable:

        bool await_ready()                          {return this->impl().isReady();}

        coro_handle await_suspend(coro_handle cur) {
            coro_handle next = this->impl().generateFor(cur);
            return lifecycle::suspendingTo(cur, typeid(this), this, next);
        }

        Result<T> await_resume()                    {return this->impl().yieldedValue();}

    private:
        friend class GeneratorImpl<T>;
        using super = Coroutine<GeneratorImpl<T>>;

        explicit Generator(typename super::handle_type handle)  :super(handle) {}
    };



    // Iterator interface to a Generator's values; not for use in coroutines, sadly
    template <typename T>
    class Generator<T>::iterator {
    public:
        iterator& operator++()      {_value = _gen.next(); return *this;}
        T const& operator*() const  {return _value.value();}
        T& operator*()              {return _value.value();}
        
        friend bool operator== (iterator const& i, std::default_sentinel_t) {
            return i._value.empty();
        }

    private:
        friend class Generator;
        explicit iterator(Generator& gen)   :_gen(gen), _value(gen.next()) { }

        Generator&  _gen;
        Result<T>   _value;
    };


#pragma mark - IMPLEMENTATION:

    
    template <typename T>
    class GeneratorImpl : public CoroutineImpl<GeneratorImpl<T>> {
    public:
        using super = CoroutineImpl<GeneratorImpl<T>>;

        GeneratorImpl() = default;

        void clear()            {_yielded_value = noerror;}

        bool isReady() const    {return _ready;}

        // Implementation of the public Generator's next() method. Called by non-coroutine code.
        Result<T> next() {
            clear();
            auto h = this->handle();
            if (h.done())
                return noerror;
            while (!_ready)
                lifecycle::resume(h);
            return yieldedValue();
        }

        // Returns the value yielded by the coroutine function after it's run.
        Result<T> yieldedValue() {
            assert(_ready);
            _ready = false;
            return std::move(_yielded_value);
        }

        // Called when the public Generator<T> is destructed.
        void stop() {
            LCoro->info("Generator {} told to stop", logCoro{this->handle()});
            // TODO: Communicate this to the function somehow...
        }

        //---- C++ coroutine internal API:

        Generator<T> get_return_object() {
            return Generator<T>(this->typedHandle());
        }

        // Invoked by the coroutine's `co_yield`. Captures the value and transfers control.
        template <std::convertible_to<T> From>
        YielderTo yield_value(From&& value) {
            _yielded_value = std::forward<From>(value);
            assert(!_yielded_value.empty());
            ready();
            auto resumer = _consumer;
            if (resumer)
                _consumer = nullptr;
            else
                resumer = CORO_NS::noop_coroutine();
            return YielderTo{resumer};
        }

        // Invoked if the coroutine throws an exception.
        void unhandled_exception() {
            this->super::unhandled_exception();
            _yielded_value = Error(std::current_exception());
            ready();
        }

        // Invoked when the coroutine fn returns, implicitly or via co_return.
        void return_void() {
            if (!_ready) {
                _yielded_value = noerror;
                ready();
            }
            lifecycle::returning(this->handle());
        }

        // Invoked when the coroutine is done.
        SuspendFinalTo<false> final_suspend() noexcept {
            assert(_ready);
            auto resumer = _consumer;
            if (resumer)
                _consumer = nullptr;
            else
                resumer = CORO_NS::noop_coroutine();
            return SuspendFinalTo<false>{resumer};
        }

    private:
        template <class U> friend class Generator;

        // Tells me which coroutine should resume after I co_yield the next value.
        coro_handle generateFor(coro_handle consumer) {
            assert(!_consumer); // multiple awaiters not supported
            _consumer = consumer;
            clear();
            return this->handle();
        }

        // Implementation of Generator::onNextResult(). Schedules a callback.
        void onReady(OnReadyFn fn) {
            if (!fn) {
                _onReady = nullptr;
            } else if (_ready) {
                fn();
            } else {
                if (!_onReady)
                    Scheduler::current().schedule(this->handle());
                _onReady = std::move(fn);
                clear();
            }
        }

        void ready() {
            _ready = true;
            if (auto onReady = std::move(_onReady)) {
                _onReady = nullptr;
                onReady();
            }
        }

        Result<T>           _yielded_value;                     // Latest value yielded
        coro_handle         _consumer;                          // Coroutine awaiting my value
        OnReadyFn           _onReady;                           // Callback when ready
        bool                _ready = false;                     // True when a value is available
    };

}
