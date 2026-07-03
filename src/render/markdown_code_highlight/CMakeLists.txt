add_library(markdown_code_highlight STATIC src/markdown_code_highlight.cpp)
target_include_directories(markdown_code_highlight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(
  markdown_code_highlight
  PUBLIC imgui
  PRIVATE tree_sitter_runtime
          tree_sitter_queries
          tree_sitter_c_grammar
          tree_sitter_cpp_grammar
          tree_sitter_python_grammar
          tree_sitter_bash_grammar
)

if(ENABLE_TEST)
  add_executable(markdown_code_highlight_tests tests/markdown_code_highlight_tests.cpp)
  target_link_libraries(markdown_code_highlight_tests PRIVATE markdown_code_highlight)
  add_test(NAME markdown_code_highlight_tests COMMAND markdown_code_highlight_tests)
endif()
