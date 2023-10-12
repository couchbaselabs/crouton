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

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # For JetBrains IDE


# Option to build the BLIP source files as a separate library.
# Disabled by default because that code is not Apache-licensed; see src/io/BLIP/README.md.
option(CROUTON_BUILD_BLIP "Build BLIP library" OFF)


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
add_subdirectory(vendor/spdlog)


#### CROUTON CONFIG


# Build spdlog as compiled code, not header-only
add_definitions(-DSPDLOG_COMPILED_LIB=1)

if (MSVC)
    # MSVC:
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX)
    add_compile_options(
        "/wd4068;/wd4100;/wd4244;/wd4267"
       # /W4         # I think this means 'turn on lots of warnings'
       # /wd4068     # ignore unknown pragma
       # /wd4100     # ignore unused fn parameters
       # /wd4244     # ignore implicit sign conversion
       # /wd4267     # ignore implicit integer truncation
    )
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
            -Wno-gnu-statement-expression           # Allow `({...})`
            -Wno-gnu-statement-expression-from-macro-expansion
        )
    endif()
endif()


#### CROUTON LIBRARY


include_directories( SYSTEM
    vendor/libuv/include/
    vendor/llhttp/include/
    vendor/mbedtls/include/
    vendor/spdlog/include/
)


add_library( LibCrouton STATIC
    src/CoCondition.cc
    src/CoroLifecycle.cc
    src/Coroutine.cc
    src/Error.cc
    src/Future.cc
    src/Logging.cc
    src/Scheduler.cc
    src/Select.cc

    src/io/AddrInfo.cc
    src/io/FileStream.cc
    src/io/Filesystem.cc
    src/io/HTTPConnection.cc
    src/io/HTTPHandler.cc
    src/io/HTTPParser.cc
    src/io/ISocket.cc
    src/io/IStream.cc
    src/io/Pipe.cc
    src/io/Process.cc
    src/io/Stream.cc
    src/io/TCPServer.cc
    src/io/TCPSocket.cc
    src/io/URL.cc
    src/io/UVBase.cc
    src/io/WebSocket.cc

    src/io/mbed/TLSSocket.cc

    src/support/Backtrace.cc
    src/support/Memoized.cc
    src/support/StringUtils.cc
)

target_include_directories( LibCrouton PUBLIC
    include/
)

target_include_directories( LibCrouton PRIVATE
    src/
    src/io/
    src/support/
)

target_link_libraries( LibCrouton INTERFACE
    uv_a
    llhttp_static
    mbedtls
    spdlog
)

set_property(TARGET LibCrouton  PROPERTY OUTPUT_NAME Crouton)

if (APPLE)
    target_sources( LibCrouton PRIVATE
        src/io/apple/NWConnection.cc
    )
    target_link_libraries( LibCrouton INTERFACE
        "-framework CoreFoundation"
        "-framework Network"
        "-framework Security"
    )
endif()

if (MSVC)
    target_sources( LibCrouton PRIVATE
        src/support/asprintf.c
        src/support/vasprintf-msvc.c
    )
endif()


#### BLIP

if (CROUTON_BUILD_BLIP)
    add_library( BLIP STATIC
        src/io/blip/BLIPConnection.cc
        src/io/blip/BLIPIO.cc
        src/io/blip/Message.cc
        src/io/blip/MessageBuilder.cc
        src/io/blip/MessageOut.cc
        src/io/blip/Codec.cc
    )

    target_include_directories( LibCrouton PUBLIC
        src/io/blip/
    )

    target_include_directories( BLIP PRIVATE
        src/
        src/io/
        src/support/
    )

    target_link_libraries( BLIP
        LibCrouton
        z
    )
endif()


#### DEMO TOOLS


if (NOT CROUTON_IOS)
    add_executable( demo_client
        tests/demo_client.cc
    )
    target_link_libraries( demo_client
        LibCrouton
    )

    add_executable( demo_server
        tests/demo_server.cc
    )
    target_link_libraries( demo_server
        LibCrouton
    )

    if (CROUTON_BUILD_BLIP)
        add_executable( demo_blipclient
            tests/demo_blipclient.cc
        )
        target_link_libraries( demo_blipclient
            LibCrouton
            BLIP
        )
    endif()

endif()


#### TESTS


if (NOT CROUTON_IOS)
    add_executable( CroutonTests
        tests/tests.cc
        tests/test_generator.cc
        tests/test_io.cc
        tests/test_http.cc
        vendor/catch2/catch_amalgamated.cpp
        vendor/catch2/ConsoleReporterPlus.cc
    )

    target_include_directories( CroutonTests PRIVATE
        src/support/
        vendor/catch2/
    )

    target_link_libraries( CroutonTests
        LibCrouton
    )

    if (CROUTON_BUILD_BLIP)
        target_sources( CroutonTests PRIVATE
            tests/test_blip.cc
        )
        target_link_libraries( CroutonTests
            BLIP
        )
    endif()
endif()

