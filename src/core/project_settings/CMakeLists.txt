add_library(project_settings STATIC src/project_settings.cpp)
target_include_directories(project_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(project_settings
  PUBLIC atomic_file
  PRIVATE nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(project_settings_tests tests/project_settings_tests.cpp)
  target_link_libraries(project_settings_tests PRIVATE project_settings nlohmann_json::nlohmann_json)
  add_test(NAME project_settings_tests COMMAND project_settings_tests)
endif()
