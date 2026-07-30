add_library(process STATIC src/process.cpp)
target_include_directories(process PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(process_tests tests/process_tests.cpp)
  target_link_libraries(process_tests PRIVATE process)
  add_test(NAME process_tests COMMAND process_tests)
  set_tests_properties(process_tests PROPERTIES TIMEOUT 20)
endif()
