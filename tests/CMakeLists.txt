FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# Header-only JSON reader for each product's own schema cross-checks.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
FetchContent_MakeAvailable(nlohmann_json)

include(CTest)
include(Catch)

if(KITFORGE_BUILD_TESTS)
add_executable(kitforge_tests
    test_param_table.cpp
    test_kit_files.cpp
    test_pattern_files.cpp
    test_sdcard.cpp
    test_midi.cpp
    test_sysex.cpp
    test_kit_ops.cpp
    test_librarian.cpp
    test_param_sync.cpp
)
target_link_libraries(kitforge_tests PRIVATE kitforge::core Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_compile_definitions(kitforge_tests PRIVATE
    KITFORGE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    KITFORGE_PARAMS_JSON="${PROJECT_SOURCE_DIR}/docs/editor-params.json"
    KITFORGE_VENDORED_PARAMS_JSON="${PROJECT_SOURCE_DIR}/docs/firmware/params.json")

catch_discover_tests(kitforge_tests)
endif()

if(VSYNTHXT_BUILD_TESTS)
add_executable(vsynthxt_tests test_vsynthxt.cpp)
target_link_libraries(vsynthxt_tests PRIVATE vsynthxt_core Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_compile_definitions(vsynthxt_tests PRIVATE
    VSYNTHXT_SCHEMA="${PROJECT_SOURCE_DIR}/docs/vsynthxt/protocol.json"
    VSYNTHXT_NATIVE_SCHEMA="${PROJECT_SOURCE_DIR}/docs/vsynthxt/native-params.json")
catch_discover_tests(vsynthxt_tests)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME vsynthxt_generated_params
        COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/gen_vsynthxt_params.py" --check)
endif()
endif()
