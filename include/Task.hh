//
// Task.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include "Scheduler.hh"

namespace crouton {
    class TaskImpl;


    /** Return type for a coroutine that doesn't return a value, just runs indefinitely.
        Unlike the base class CoroutineHandle, it does not destroy the coroutine handle in its
        destructor. */
    class Task : public CoroutineHandle<TaskImpl> {
    public:
        ~Task()     {setHandle(nullptr);}   // don't let parent destructor destroy the coroutine
    protected:
        friend class Scheduler;
    private:
        friend class TaskImpl;
        explicit Task(handle_type h) :CoroutineHandle<TaskImpl>(h) { }
    };



    class TaskImpl : public CoroutineImpl<Task, TaskImpl> {
    public:
        ~TaskImpl() = default;
        Task get_return_object()                {return Task(handle());}
        std::suspend_never initial_suspend() {
            //std::cerr << "New " << typeid(this).name() << " " << handle() << std::endl;
            return {};
        }
        Yielder yield_value(bool)               { return Yielder(handle()); }
        void return_void()                      { }
    private:
    };
}
