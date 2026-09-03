add_library(note_diff STATIC src/note_diff.cpp)
target_include_directories(note_diff PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(note_diff_tests tests/note_diff_tests.cpp)
  target_link_libraries(note_diff_tests PRIVATE note_diff)
  add_test(NAME note_diff_tests COMMAND note_diff_tests)
  set_tests_properties(note_diff_tests PROPERTIES TIMEOUT 60)
endif()
