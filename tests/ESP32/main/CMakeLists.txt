idf_component_register(
    SRCS
    "main.cc"

#    INCLUDE_DIRS
#    "../../../include/"
#    "../../../vendor/llhttp/include/"
#    "../../../vendor/spdlog/include/"
#    "../../../src/"
#    "../../../src/io/mbed/"
#    "../../../src/support/"
#    "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-UNDEBUG"
    "-Wno-unknown-pragmas"      # ignore Xcode `#pragma mark` in source code
)
