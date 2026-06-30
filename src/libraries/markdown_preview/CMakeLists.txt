add_library(markdown_preview STATIC src/markdown_view.cpp)
target_include_directories(markdown_preview
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/app/inc
)
target_link_libraries(markdown_preview
  PUBLIC
    imgui
    imgui_md
    note_core
    markdown_code_highlight
    markdown_model
    SDL2_image::SDL2_image
    OpenGL::GL
)
