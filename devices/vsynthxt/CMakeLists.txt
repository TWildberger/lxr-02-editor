add_library(vsynthxt_core STATIC VSynthXT.cpp Parameters.cpp Algorithms.cpp)
target_include_directories(vsynthxt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${PROJECT_SOURCE_DIR}/shared/include")
target_link_libraries(vsynthxt_core PRIVATE companion_sqlite)
