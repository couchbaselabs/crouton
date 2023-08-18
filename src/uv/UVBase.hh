//
// UVBase.hh
//
// 
//

#pragma once
#include <stdexcept>
#include <functional>

struct uv_timer_s;

namespace snej::coro::uv {

    class UVError : public std::runtime_error {
    public:
        explicit UVError(int status);
        int err;
    };


    class Timer {
    public:
        Timer(std::function<void()> fn);
        ~Timer();

        void start(double delaySecs, double repeatSecs = 0);

        void stop();

        static void after(double delaySecs, std::function<void()> fn);

    private:
        std::function<void()> _fn;
        uv_timer_s* _handle = nullptr;
        bool _deleteMe = false;
    };


    void OnEventLoop(std::function<void()>);
}
