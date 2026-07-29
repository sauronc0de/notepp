add_library(markdown_widgets STATIC
  src/button_action.cpp
  src/markdown_widgets.cpp
)
target_include_directories(markdown_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_widgets PUBLIC imgui string_utils markdown_view SDL2_image::SDL2_image OpenGL::GL PRIVATE atomic_file)

if(ENABLE_TEST)
  add_executable(markdown_widgets_tests tests/markdown_widgets_tests.cpp)
  target_include_directories(markdown_widgets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(markdown_widgets_tests PRIVATE markdown_widgets)
  add_test(NAME markdown_widgets_tests COMMAND markdown_widgets_tests)
endif()
