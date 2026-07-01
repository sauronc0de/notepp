add_library(string_utils STATIC src/string_utils.cpp)
target_include_directories(string_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(string_utils_tests tests/string_utils_tests.cpp)
  target_link_libraries(string_utils_tests PRIVATE string_utils)
  add_test(NAME string_utils_tests COMMAND string_utils_tests)
endif()
