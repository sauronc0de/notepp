add_library(markdown_sections STATIC src/markdown_sections.cpp)
target_include_directories(markdown_sections PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_sections PUBLIC string_utils)

if(ENABLE_TEST)
  add_executable(markdown_sections_tests tests/markdown_sections_tests.cpp)
  target_link_libraries(markdown_sections_tests PRIVATE markdown_sections)
  add_test(NAME markdown_sections_tests COMMAND markdown_sections_tests)
endif()
