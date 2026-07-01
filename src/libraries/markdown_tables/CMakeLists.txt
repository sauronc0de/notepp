add_library(markdown_tables STATIC src/markdown_tables.cpp)
target_include_directories(markdown_tables PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_tables PUBLIC string_utils)

if(ENABLE_TEST)
  add_executable(markdown_tables_tests tests/markdown_tables_tests.cpp)
  target_link_libraries(markdown_tables_tests PRIVATE markdown_tables)
  add_test(NAME markdown_tables_tests COMMAND markdown_tables_tests)
endif()