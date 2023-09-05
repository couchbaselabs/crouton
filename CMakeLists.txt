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
set(MBEDTLS_FATAL_WARNINGS OFF CACHE INTERNAL "") # Work around doc-comment syntax in 3.4

add_subdirectory(vendor/libuv)
add_subdirectory(vendor/llhttp)
add_subdirectory(vendor/mbedtls)

find_package(ZLIB REQUIRED)


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
            -Wno-gnu-zero-variadic-macro-arguments
            -Wno-gnu-conditional-omitted-operand    # Allow `x ?: y`
        )
    endif()
endif()


#### CROUTON LIBRARY


include_directories( SYSTEM
    vendor/libuv/include/
    vendor/llhttp/include/
    vendor/mbedtls/include/
)


include_directories(
    include/
    include/apple/
    include/mbedtls/
    src/
    src/support/
)

add_library( LibCrouton STATIC
    src/AddrInfo.cc
    src/FileStream.cc
    src/Filesystem.cc
    src/Future.cc
    src/HTTPConnection.cc
    src/HTTPParser.cc
    src/IStream.cc
    src/Pipe.cc
    src/Scheduler.cc
    src/Stream.cc
    src/TCPServer.cc
    src/TCPSocket.cc
    src/URL.cc
    src/UVBase.cc
    src/WebSocket.cc
    src/mbedtls/TLSSocket.cc
    src/support/Backtrace.cc
)

set_property(TARGET LibCrouton  PROPERTY OUTPUT_NAME Crouton)

target_link_libraries( LibCrouton INTERFACE
    uv_a
    llhttp_static
    mbedtls
)

if(APPLE)
    target_sources( LibCrouton PRIVATE
        src/apple/NWConnection.cc
    )
    target_link_libraries( LibCrouton INTERFACE
        "-framework CoreFoundation"
        "-framework Network"
        "-framework Security"
    )
endif()


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
        tests/test_http.cc
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

