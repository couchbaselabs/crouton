//
//  tests.cc
//  coro
//
//  Created by Jens Alfke on 8/16/23.
//

#include "coro.hh"

#include "catch_amalgamated.hpp"
#include <iostream>

using namespace NAMESPACE;

TEST_CASE("boilerplate") {
    CHECK(boilerplate() == 42);
}
