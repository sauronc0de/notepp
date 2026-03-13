add_library(note_ui STATIC note_ui.cpp)
target_include_directories(note_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(note_ui PUBLIC note_core imgui OpenGL::GL SDL2_image::SDL2_image)

