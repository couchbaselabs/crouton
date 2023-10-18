set(root "../../..")
set(src  "../..")

idf_component_register(
    SRCS
        "${src}/Coroutine.cc"
        "${src}/CoCondition.cc"
        "${src}/CoroLifecycle.cc"
        "${src}/Coroutine.cc"
        "${src}/Error.cc"
        "${src}/Future.cc"
        "${src}/Internal.hh"
        "${src}/Logging.cc"
        "${src}/Scheduler.cc"
        "${src}/Select.cc"
        "${src}/io/HTTPHandler.cc"
        "${src}/io/HTTPParser.cc"
        "${src}/io/ISocket.cc"
        "${src}/io/IStream.cc"
        "${src}/io/URL.cc"
        "${src}/io/WebSocket.cc"
        "${src}/io/mbed/TLSSocket.cc"
        "${src}/support/Backtrace.cc"
        "${src}/support/betterassert.cc"
        "${src}/support/Memoized.cc"
        "${src}/support/StringUtils.cc"
        
        "Backtrace+ESP32.cc"
        "ESPBase.cc"
        "ESPEventLoop.cc"
        "ESPAddrInfo.cc"
        "ESPTCPSocket.cc"

    INCLUDE_DIRS
        "${root}/include/"

    PRIV_INCLUDE_DIRS
        "${src}/"
        "${src}/support/"
        "${root}/vendor/llhttp/include/"
        "${root}/vendor/spdlog/include/"
        "."

    REQUIRES
        lwip
        mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-Wno-unknown-pragmas"      # ignore Xcode `#pragma mark` in source code
)
