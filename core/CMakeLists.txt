add_library(kitforge_core STATIC
    src/Bytes.cpp
    src/Kit.cpp
    src/KitOps.cpp
    src/Librarian.cpp
    src/Midi.cpp
    src/ParamSync.cpp
    src/ParamTable.cpp
    src/Pattern.cpp
    src/SdCard.cpp
    src/SysEx.cpp
    src/generated/ParamTable.gen.cpp
)
add_library(kitforge::core ALIAS kitforge_core)

target_include_directories(kitforge_core PUBLIC include)

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(kitforge_core PRIVATE SQLite3::SQLite3)
    else()
        target_link_libraries(kitforge_core PRIVATE SQLite::SQLite3)
    endif()
else()
    message(STATUS "System SQLite3 not found, fetching the amalgamation")
    FetchContent_Declare(sqlite_amalgamation
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
        URL_HASH SHA3_256=af6aae8d3eccc608857c63cf56efbadc70da48b5c719446b353ed88dded1e288)
    FetchContent_MakeAvailable(sqlite_amalgamation)
    add_library(kitforge_sqlite STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
    target_include_directories(kitforge_sqlite PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
    target_compile_definitions(kitforge_sqlite PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)
    target_link_libraries(kitforge_core PRIVATE kitforge_sqlite)
endif()

if(MSVC)
    target_compile_options(kitforge_core PRIVATE /W4)
else()
    target_compile_options(kitforge_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()
