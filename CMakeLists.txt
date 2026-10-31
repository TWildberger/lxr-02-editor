cmake_minimum_required(VERSION 3.22)

project(KitForge VERSION 0.1.0 LANGUAGES C CXX)

option(KITFORGE_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(KITFORGE_BUILD_TESTS "Build unit tests" ON)
option(VSYNTHXT_BUILD_PLUGIN "Build the V-Synth XT companion (VST3/AU/Standalone)" ${KITFORGE_BUILD_PLUGIN})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

add_subdirectory(core)
add_subdirectory(devices/vsynthxt)

if(KITFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KITFORGE_BUILD_PLUGIN OR VSYNTHXT_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
