add_library(app_settings STATIC src/app_settings.cpp)
target_include_directories(app_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(app_settings PUBLIC atomic_file PRIVATE nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(app_settings_tests tests/app_settings_tests.cpp)
  target_link_libraries(app_settings_tests PRIVATE app_settings nlohmann_json::nlohmann_json)
  add_test(NAME app_settings_tests COMMAND app_settings_tests)
endif()
