add_library(markdown_widgets STATIC src/markdown_widgets.cpp)
target_include_directories(markdown_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_widgets PUBLIC imgui string_utils markdown_view SDL2_image::SDL2_image OpenGL::GL)

if(ENABLE_TEST)
  add_executable(json_widgets_tests tests/json_widgets_tests.cpp)
  target_link_libraries(json_widgets_tests PRIVATE markdown_widgets)
  add_test(NAME json_widgets_tests COMMAND json_widgets_tests)
endif()
