add_library(note_clipboard_state STATIC src/note_clipboard_state.cpp)
target_include_directories(note_clipboard_state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(note_clipboard_state PUBLIC nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(note_clipboard_state_tests tests/note_clipboard_state_tests.cpp)
  target_link_libraries(note_clipboard_state_tests PRIVATE note_clipboard_state)
  add_test(NAME note_clipboard_state_tests COMMAND note_clipboard_state_tests)
endif()
