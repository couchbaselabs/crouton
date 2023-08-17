//
// UVBase.hh
//
// 
//

#pragma once
#include <stdexcept>

namespace snej::coro::uv {

    class UVError : public std::runtime_error {
    public:
        explicit UVError(int status);
        int err;
    };

}
