add_library(note_project STATIC src/note_project.cpp)
target_include_directories(note_project PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(note_project PUBLIC tiny_json nfd log PRIVATE nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(note_project_tests tests/note_project_tests.cpp)
  target_link_libraries(note_project_tests PRIVATE note_project)
  add_test(NAME note_project_tests COMMAND note_project_tests)
endif()
