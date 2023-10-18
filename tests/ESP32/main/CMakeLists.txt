idf_component_register(
    SRCS
        "main.cc"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-UNDEBUG"
    "-Wno-unknown-pragmas"      # ignore Xcode `#pragma mark` in source code
)
