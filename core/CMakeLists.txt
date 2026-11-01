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

target_include_directories(kitforge_core PUBLIC include "${PROJECT_SOURCE_DIR}/shared/include")
target_link_libraries(kitforge_core PRIVATE companion_sqlite)

if(MSVC)
    target_compile_options(kitforge_core PRIVATE /W4)
else()
    target_compile_options(kitforge_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()
