add_library(note_history STATIC src/undo_redo.cpp)
target_include_directories(note_history PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(note_history_tests tests/note_history_tests.cpp)
  target_link_libraries(note_history_tests PRIVATE note_history)
  add_test(NAME note_history_tests COMMAND note_history_tests)
endif()
