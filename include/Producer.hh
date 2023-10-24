//
// Producer.hh
//
// Copyright © 2023-Present Couchbase, Inc. All rights reserved.
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
#include "Awaitable.hh"
#include "Coroutine.hh"
#include "util/Relation.hh"
#include "Result.hh"

namespace crouton {
    template <typename T> class SeriesConsumer;


    /** Paired with a SeriesConsumer.
        Items passed to `produce()` will be received as a series by the consumer, an ISeries. */
    template <typename T>
    class SeriesProducer {
    public:
        SeriesProducer() noexcept    :_consumer(this) { }
        SeriesProducer(SeriesProducer&&) noexcept = default;
        SeriesProducer& operator=(SeriesProducer&&) noexcept = default;
        ~SeriesProducer()   {assert(!_consumer);}

        /// Creates the SeriesConsumer. Can only be called once.
        std::unique_ptr<SeriesConsumer<T>> make_consumer() {
            precondition(!_consumer);
            return std::unique_ptr<SeriesConsumer<T>>(new SeriesConsumer<T>(this));
        }

        /// Return value of `produce()`. Must be awaited; result is false if consumer is gone.
        class AwaitProduce {
        public:
            bool await_ready() noexcept {
                return !self._consumer || !self._consumer->_hasValue;
            }

            coro_handle await_suspend(coro_handle cur) {
                assert(!self._suspension);
                self._suspension = Scheduler::current().suspend(cur);
                return lifecycle::suspendingTo(cur, CRTN_TYPEID(self), &self, nullptr);
            }

            [[nodiscard]] bool await_resume() {
                if (self._consumer)
                    self._consumer->setValue(std::move(value));
                return (self._consumer != nullptr);
            }
        private:
            friend class SeriesProducer;
            AwaitProduce(SeriesProducer<T>* p, Result<T> v) :self(*p), value(std::move(v)) { }
            SeriesProducer<T>& self;
            Result<T>          value;
        };

        /// Adds a value to the series, to be read by a waiting SeriesConsumer.
        /// **Must be awaited.** Suspends until the SeriesConsumer reads the previous value.
        /// `co_await` returns true if the SeriesConsumer still exists, false if it's been destroyed.
        [[nodiscard]] AwaitProduce produce(Result<T> value) {
            precondition(!_consumer || !_consumer->_eof);
            return AwaitProduce(this, std::move(value));
        }

    private:
        friend class SeriesConsumer<T>;

        util::OneToOne<SeriesProducer<T>,SeriesConsumer<T>> _consumer;    // Bidi link to consumer
        Suspension                    _suspension;              // Coroutine blocked in produce()
    };



    /** An ISeries implementation that generates values fed to it by its SeriesProducer. */
    template <typename T>
    class SeriesConsumer final : public ISeries<T> {
    public:
        SeriesConsumer(SeriesConsumer&&) noexcept = default;
        SeriesConsumer& operator=(SeriesConsumer&&) noexcept = default;

        bool await_ready() noexcept override {
            precondition(_producer);
            return _hasValue;
        }

        coro_handle await_suspend(coro_handle cur) override {
            _suspension = Scheduler::current().suspend(cur);
            return lifecycle::suspendingTo(cur, CRTN_TYPEID(*this), this, nullptr);
        }

        [[nodiscard]] Result<T> await_resume() override {
            // Take the value from the SeriesProducer:
            assert(_hasValue);
            _hasValue = false;
            Result<T> value = std::move(_value);
            // Wake up the producer if it was blocked:
            if (_producer)
                _producer->_suspension.wakeUp();
            return value;
        }

        void onReady(OnReadyFn fn) override {
            if (!fn) {
                _onReady = nullptr;
            } else if (_hasValue) {
                fn();
            } else {
                _onReady = std::move(fn);
            }
        }

    private:
        friend class SeriesProducer<T>;

        explicit SeriesConsumer(SeriesProducer<T>* prod) :_producer(this, &prod->_consumer) { }

        void setValue(Result<T> value) {
            assert(!_hasValue);
            _hasValue = true;
            _eof = !value.ok();
            _value = std::move(value);
            if (_eof)
                _producer = nullptr;
            // Call any onReady callback:
            if (auto onReady = std::move(_onReady)) {
                _onReady = nullptr;
                onReady();
            }
            // Wake up any waiting coroutine:
            _suspension.wakeUp();
        }

        util::OneToOne<SeriesConsumer<T>,SeriesProducer<T>> _producer;// Bidi link to producer
        Suspension                    _suspension;          // Suspended coroutine
        Result<T>                     _value;               // Current value (if _hasValue is true)
        OnReadyFn                     _onReady;             // Callback when ready
        bool                          _hasValue = false;    // True when a value is available
        bool                          _eof = false;         // True when final value produced
    };

}
