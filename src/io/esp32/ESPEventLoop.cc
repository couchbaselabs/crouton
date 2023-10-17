//
// ESPEventLoop.cc
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

#include "EventLoop.hh"
#include "Logging.hh"
#include "Task.hh"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

#include <functional>

namespace crouton {
    using namespace std;

    using AsyncFn = std::function<void()>;

    /** Size of FreeRTOS event queue, per task. */
    static constexpr size_t kQueueLength = 16;

    /** The struct that goes in the FreeRTOS event queue.
        Can't use any fancy C++ RIAA stuff because these get memcpy'd by FreeRTOS. */
    struct Event {
        enum Type : uint8_t {
            Interrupt,
            Async,
            TimerFired,
        };
        Type type = Interrupt;
        union {
            AsyncFn* asyncFn;
            Timer*   timer;
        } data;
    };

    
    /** Implementation of EventLoop for ESP32. */
    class ESPEventLoop final : public EventLoop {
    public:
        ESPEventLoop()
        :_queue(xQueueCreate(kQueueLength, sizeof(Event)))
        {
            if (!_queue) throw std::bad_alloc();
            LLoop->trace("Created ESPEventLoop");
        }

        void run() override {
            do {
                runOnce();
            } while (!_interrupt);
            _interrupt = false;
        }

        bool runOnce(bool waitForIO =true) override {
            LLoop->trace("runOnce...");
            _interrupt = false;
            Event event;
            if (xQueueReceive(_queue, &event, waitForIO ? portMAX_DELAY : 0) == pdPASS) {
                switch (event.type) {
                    case Event::Interrupt:
                        LLoop->trace("    received Interrupt event");
                        _interrupt = true;
                        break;
                    case Event::Async:
                        LLoop->trace("    received AsyncFn event");
                        try {
                            (*event.data.asyncFn)();
                        } catch (...) {
                            LLoop->error("*** Unexpected exception in EventLoop::perform callback ***");
                        }
                        delete event.data.asyncFn;
                        break;
                    case Event::TimerFired:
                        LLoop->trace("    received TimerFired event");
                        fireTimer(event.data.timer);
                        break;
                    default:
                        LLoop->error("Received nknown event type {}", uint8_t(event.type));
                        break;
                }
            }
            LLoop->trace("...runOnce returning; {} msgs waiting",
                         uxQueueMessagesWaiting(_queue));
            return uxQueueMessagesWaiting(_queue) > 0;
        }

        void stop(bool threadSafe) override {
            LLoop->trace("stop!");
            post(Event{.type = Event::Interrupt, .data = {}});
        }

        void perform(std::function<void()> fn) override {
            LLoop->trace("posting perform");
            post(Event{
                .type = Event::Async,
                .data = {.asyncFn = new AsyncFn(std::move(fn))}
            });
        }

        void post(Event const& event) {
            if (xQueueSendToBack(_queue, &event, 0) != pdPASS)
                throw runtime_error("Event queue full!");
        }

    private:
        QueueHandle_t   _queue;
        bool            _interrupt = false;
    };


    EventLoop* Scheduler::newEventLoop() {
        return new ESPEventLoop();
    }


#pragma mark - TIMER:


    static unsigned ms(double secs){
        return unsigned(::round(max(secs, 0.0) * 1000.0));
    }

    static TickType_t ticks(double secs){
        return pdMS_TO_TICKS(ms(secs));
    }


    Timer::Timer(std::function<void()> fn)
    :_fn(std::move(fn))
    ,_eventLoop(&Scheduler::current().eventLoop())
    ,_impl(nullptr)
    { }


    Timer::~Timer() {
        if (_impl) stop();
    }


    void Timer::_start(double delaySecs, double repeatSecs) {
        LLoop->trace("Timer::start({}, {})", delaySecs, repeatSecs);
        precondition(!_impl && (repeatSecs == 0.0 || repeatSecs == delaySecs));
        auto callback = [](TimerHandle_t timer) {
            // The timer callback runs on a special FreeRTOS task.
            // From here we post an event to the target task's EventLoop:
            auto self = (Timer*)pvTimerGetTimerID(timer);
            ((ESPEventLoop*)self->_eventLoop)->post(Event{
                .type = Event::TimerFired,
                .data = {.timer = self}
            });
        };
        _impl = xTimerCreate("Timer", ticks(delaySecs), (repeatSecs > 0), this, callback);
        if (!_impl) throw std::bad_alloc();
        xTimerStart(TimerHandle_t(_impl), portMAX_DELAY);
    }


    void Timer::_fire() {
        LLoop->trace("Timer fired! Calling fn...");
        try {
            _fn();
            LLoop->trace("...Timer fn returned");
        } catch (exception const& x) {
            LLoop->error("*** Caught unexpected exception in Timer callback: {}", x.what());
        }
        if (_deleteMe)
            delete this;
    }


    void Timer::stop() {
        if (_impl) {
            LLoop->trace("Timer::stop");
            xTimerDelete(TimerHandle_t(_impl), portMAX_DELAY);
            _impl = nullptr;
        }
    }


    /*static*/ void Timer::after(double delaySecs, std::function<void()> fn) {
        auto t = new Timer(std::move(fn));
        t->_deleteMe = true;
        t->once(delaySecs);
    }


    Future<void> Timer::sleep(double delaySecs) {
        auto provider = Future<void>::provider();
        Timer::after(delaySecs, [provider]{provider->setResult();});
        return Future(provider);
    }


#pragma mark - ON BACKGROUND THREAD:


    Future<void> OnBackgroundThread(std::function<void()> fn) {
        Error(CroutonError::Unimplemented).raise();  // TODO
    }

}
