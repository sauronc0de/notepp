add_library(markdown_code_highlight STATIC markdown_code_highlight.cpp)
target_include_directories(markdown_code_highlight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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
