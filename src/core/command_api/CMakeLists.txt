add_library(command_api STATIC src/command_api.cpp)
target_include_directories(command_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(command_api PUBLIC
  atomic_file
  markdown_sections
  note_index
  note_storage
  note_project
  nlohmann_json::nlohmann_json
)

if(ENABLE_TEST)
  add_executable(command_api_tests tests/command_api_tests.cpp)
  target_link_libraries(command_api_tests PRIVATE command_api)
  add_test(NAME command_api_tests COMMAND command_api_tests)
endif()
