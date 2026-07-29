add_library(project_paths STATIC src/project_paths.cpp)
target_include_directories(project_paths PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(project_paths_tests tests/project_paths_tests.cpp)
  target_link_libraries(project_paths_tests PRIVATE project_paths)
  add_test(NAME project_paths_tests COMMAND project_paths_tests)
endif()
