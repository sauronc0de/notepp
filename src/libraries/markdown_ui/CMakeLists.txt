add_library(markdown_ui STATIC src/markdown_ui.cpp)
target_include_directories(markdown_ui
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/app/inc
)
target_link_libraries(markdown_ui PUBLIC imgui string_utils markdown_view SDL2_image::SDL2_image OpenGL::GL)
