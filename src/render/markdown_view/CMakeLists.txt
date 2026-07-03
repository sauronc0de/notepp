add_library(markdown_view STATIC src/markdown_view.cpp)
target_include_directories(markdown_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_view
  PUBLIC
    imgui
    imgui_md
    string_utils
    markdown_code_highlight
    markdown_sections
    SDL2_image::SDL2_image
    OpenGL::GL
)
