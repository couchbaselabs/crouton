# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# This example uses the `protocol_examples_common` component for Wi-Fi setup,
# as well as the Crouton component (of course).
set(EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/common_components/protocol_examples_common
    ../../src/io/esp32
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_crouton)
