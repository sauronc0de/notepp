add_library(markdown_support STATIC src/markdown_support.cpp)
target_include_directories(markdown_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_support PUBLIC string_utils mermaid imgui nlohmann_json::nlohmann_json markdown_widgets markdown_sections markdown_view markdown_editor markdown_tables project_paths PRIVATE atomic_file)

if(ENABLE_TEST)
  add_executable(markdown_support_tests tests/markdown_support_tests.cpp)
  target_link_libraries(markdown_support_tests PRIVATE markdown_support)
  add_test(NAME markdown_support_tests COMMAND markdown_support_tests)
endif()
