add_library(layout_profile_state STATIC src/layout_profile_state.cpp)
target_include_directories(layout_profile_state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(layout_profile_state PRIVATE nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(layout_profile_state_tests tests/layout_profile_state_tests.cpp)
  target_link_libraries(layout_profile_state_tests PRIVATE layout_profile_state nlohmann_json::nlohmann_json)
  add_test(NAME layout_profile_state_tests COMMAND layout_profile_state_tests)
endif()
