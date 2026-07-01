add_library(emoji_picker STATIC src/emoji_picker.cpp)
target_include_directories(emoji_picker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(emoji_picker PUBLIC imgui)

if(ENABLE_TEST)
  add_executable(emoji_picker_tests tests/emoji_picker_tests.cpp)
  target_link_libraries(emoji_picker_tests PRIVATE emoji_picker)
  add_test(NAME emoji_picker_tests COMMAND emoji_picker_tests)
endif()
