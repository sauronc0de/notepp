add_library(lang STATIC src/lang.cpp)
target_include_directories(lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(lang PUBLIC nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(lang_tests tests/lang_tests.cpp)
  target_link_libraries(lang_tests PRIVATE lang)
  add_test(NAME lang_tests COMMAND lang_tests)
endif()
