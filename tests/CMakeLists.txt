FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

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
target_link_libraries(kitforge_tests PRIVATE kitforge::core Catch2::Catch2WithMain)
target_compile_definitions(kitforge_tests PRIVATE
    KITFORGE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    KITFORGE_PARAMS_JSON="${PROJECT_SOURCE_DIR}/docs/params.json")

include(CTest)
include(Catch)
catch_discover_tests(kitforge_tests)
