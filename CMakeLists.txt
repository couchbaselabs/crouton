cmake_minimum_required(VERSION 3.9)

# MacOS deployment target must be set before the `project` call
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE INTERNAL "")
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

project( Crouton
    VERSION     0.1.0
    DESCRIPTION "A C++20 coroutine runtime and libuv-based I/O library"
    LANGUAGES   C CXX
)


set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)


#### CMAKE SUBPROJECTS


# libuv options:
set(LIBUV_BUILD_SHARED  OFF CACHE INTERNAL "")

# llhttp options:
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")

# mbedTLS options:
set(ENABLE_PROGRAMS  OFF CACHE INTERNAL "")
set(ENABLE_TESTING  OFF CACHE INTERNAL "")

# tlsuv options:
# set(TLSUV_TLSLIB  mbedtls CACHE INTERNAL "")

add_subdirectory(vendor/libuv)
add_subdirectory(vendor/llhttp)
add_subdirectory(vendor/mbedtls)
# add_subdirectory(vendor/tlsuv)    # Nope, I'm building tlsuv myself (below)

find_package(ZLIB REQUIRED)


#### TLSUV


include_directories(
    vendor/libuv/include/
    vendor/tlsuv/include/tlsuv
)

include_directories( SYSTEM
    vendor/llhttp/include/
    vendor/tlsuv/deps/uv_link_t/include
)


# I cannot figure out how to build tlsuv with CMake, specifically how to tell it where
# llhttp is, so I'll just build it myself.

add_library( TLSuv STATIC
    vendor/tlsuv/src/tlsuv.c
    vendor/tlsuv/src/bio.c
    vendor/tlsuv/src/http.c
    vendor/tlsuv/src/tcp_src.c
    vendor/tlsuv/src/um_debug.c
    vendor/tlsuv/src/um_debug.h
    vendor/tlsuv/src/websocket.c
    vendor/tlsuv/src/http_req.c
    vendor/tlsuv/src/tls_link.c
    vendor/tlsuv/src/base64.c
    vendor/tlsuv/src/tls_engine.c
    vendor/tlsuv/src/compression.c
    vendor/tlsuv/src/compression.h
    vendor/tlsuv/src/p11.c
    vendor/tlsuv/src/p11.h
    vendor/tlsuv/src/mbedtls/engine.c
    vendor/tlsuv/src/mbedtls/keys.c
    vendor/tlsuv/src/mbedtls/mbed_p11.c
    vendor/tlsuv/src/mbedtls/p11_ecdsa.c
    vendor/tlsuv/src/mbedtls/p11_rsa.c
    vendor/tlsuv/deps/uv_link_t/src/uv_link_observer_t.c
    vendor/tlsuv/deps/uv_link_t/src/uv_link_source_t.c
    vendor/tlsuv/deps/uv_link_t/src/defaults.c
    vendor/tlsuv/deps/uv_link_t/src/uv_link_t.c
)

target_compile_options( TLSuv PRIVATE
    -Wno-pedantic
    -Wno-gnu-zero-variadic-macro-arguments
    -Wno-unused-function
    -Wno-unused-variable
)

target_include_directories( TLSuv SYSTEM PUBLIC
    vendor/mbedtls/include/
    vendor/tlsuv/include/
    vendor/tlsuv/deps/uv_link_t/
    vendor/tlsuv/src/
)

target_link_libraries(TLSuv PRIVATE
    uv_a
    llhttp_static
    ZLIB::ZLIB
)

# use mbedtls:
target_compile_definitions(TLSuv PRIVATE USE_MBEDTLS)
target_include_directories(TLSuv PUBLIC vendor/mbedtls/include)
target_link_libraries(TLSuv PRIVATE mbedtls)


#### CROUTON CONFIG


if (MSVC)
    # MSVC:
    add_compile_options(/W4) # TODO: Add /WX
else()
    # Clang & GCC:
    add_compile_options(
        -Werror
        -Wall
        -Wpedantic
        -Wno-unknown-pragmas
        -Wno-unknown-warning-option
    )
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        # GCC-specific:
        add_compile_options(
            -Wno-psabi          # suppress annoying GCC ABI warning
            -Wno-sign-compare   # apparently comes with `pedantic` in GCC
            -D_FORTIFY_SOURCE=2 # static+dynamic buffer-overflow checks
        )
    else()
        # Clang-specific:
        add_compile_options(
             -Wno-gnu-conditional-omitted-operand    # Allow `x ?: y`
        )
    endif()
endif()


#### CROUTON LIBRARY


include_directories(
    include/
    src/
    src/support/
)

add_library( LibCrouton STATIC
    src/AddrInfo.cc
    src/AsyncFile.cc
    src/Future.cc
    src/HTTPClient.cc
    src/Pipe.cc
    src/Scheduler.cc
    src/Stream.cc
    src/TCPServer.cc
    src/TCPSocket.cc
    src/URL.cc
    src/UVBase.cc
    src/WebSocket.cc
    src/support/Backtrace.cc
)

set_property(TARGET LibCrouton  PROPERTY OUTPUT_NAME Crouton)

target_link_libraries( LibCrouton INTERFACE
    TLSuv
)


#### DEMO TOOLS


if (NOT CROUTON_IOS)

    add_executable( testclient
        tests/testclient.cc
    )

    target_link_libraries( testclient
        LibCrouton
    )

    add_executable( testserver
        tests/testserver.cc
    )

    target_link_libraries( testserver
        LibCrouton
    )

endif()


#### TESTS


if (NOT CROUTON_IOS)

    add_executable( CroutonTests
        tests/tests.cc
        tests/test_io.cc
        vendor/catch2/catch_amalgamated.cpp
    )

    target_include_directories( CroutonTests PRIVATE
        vendor/catch2/
    )

    target_link_libraries( CroutonTests
        LibCrouton
    )

endif()

