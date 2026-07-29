add_library(atomic_file STATIC src/atomic_file.cpp)
target_include_directories(atomic_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(atomic_file_tests tests/atomic_file_tests.cpp)
  target_link_libraries(atomic_file_tests PRIVATE atomic_file)
  add_test(NAME atomic_file_tests COMMAND atomic_file_tests)
endif()
