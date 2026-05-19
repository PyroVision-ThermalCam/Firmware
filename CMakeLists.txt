cmake_minimum_required(VERSION 3.16.0)

# Get version from git describe
execute_process(
    COMMAND git describe --always --tags --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PROJECT_VER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Fallback if git not available
if(NOT PROJECT_VER)
    set(PROJECT_VER "X.Y.Z-dev")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(PyroVision)
