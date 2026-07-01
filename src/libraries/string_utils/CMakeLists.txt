add_library(string_utils STATIC src/string_utils.cpp)
target_include_directories(string_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
