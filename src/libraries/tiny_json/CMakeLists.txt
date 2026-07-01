add_library(tiny_json STATIC src/tiny_json.cpp)
target_include_directories(tiny_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(tiny_json_tests tests/tiny_json_tests.cpp)
  target_link_libraries(tiny_json_tests PRIVATE tiny_json)
  add_test(NAME tiny_json_tests COMMAND tiny_json_tests)
endif()
