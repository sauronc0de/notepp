add_library(markdown_editor STATIC src/markdown_editor.cpp)
target_include_directories(markdown_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_editor PUBLIC string_utils imgui)

if(ENABLE_TEST)
  add_executable(markdown_editor_tests tests/markdown_editor_tests.cpp)
  target_link_libraries(markdown_editor_tests PRIVATE markdown_editor)
  add_test(NAME markdown_editor_tests COMMAND markdown_editor_tests)
endif()