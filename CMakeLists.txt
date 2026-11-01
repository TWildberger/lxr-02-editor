cmake_minimum_required(VERSION 3.22)

project(KitForge VERSION 0.1.0 LANGUAGES C CXX)

option(KITFORGE_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(KITFORGE_BUILD_TESTS "Build unit tests" ON)
option(KITFORGE_BUILD_CORE "Build the LXR-02 core even without its plugin/tests" ON)
option(VSYNTHXT_BUILD_PLUGIN "Build the V-Synth XT companion (VST3/AU/Standalone)" ${KITFORGE_BUILD_PLUGIN})
option(VSYNTHXT_BUILD_TESTS "Build XT unit and plugin tests" ${KITFORGE_BUILD_TESTS})
option(VSYNTHXT_BUILD_CORE "Build the XT core even without its plugin/tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(KITFORGE_BUILD_CORE OR KITFORGE_BUILD_PLUGIN OR KITFORGE_BUILD_TESTS
    OR VSYNTHXT_BUILD_CORE OR VSYNTHXT_BUILD_PLUGIN OR VSYNTHXT_BUILD_TESTS)
    include(cmake/SQLite.cmake)
endif()
if(KITFORGE_BUILD_CORE OR KITFORGE_BUILD_PLUGIN OR KITFORGE_BUILD_TESTS)
    add_subdirectory(core)
endif()
if(VSYNTHXT_BUILD_CORE OR VSYNTHXT_BUILD_PLUGIN OR VSYNTHXT_BUILD_TESTS)
    add_subdirectory(devices/vsynthxt)
endif()

if(KITFORGE_BUILD_TESTS OR VSYNTHXT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KITFORGE_BUILD_PLUGIN OR VSYNTHXT_BUILD_PLUGIN)
    set(COMPANION_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND COMPANION_FORMATS AU)
    endif()
    add_subdirectory(plugin)
    include(cmake/Packaging.cmake)
endif()
