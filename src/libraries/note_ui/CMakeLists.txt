add_library(note_ui STATIC src/note_ui.cpp src/icon_shader.cpp)
# icon_shader.cpp must be compiled in isolation: it includes GL/glew.h which
# must precede any other GL header. Unity Build merges files and causes the
# SDL_opengl.h vs glew.h include-order conflict.
set_source_files_properties(src/icon_shader.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_include_directories(note_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(note_ui PUBLIC note_core imgui OpenGL::GL SDL2_image::SDL2_image GLEW::GLEW)
