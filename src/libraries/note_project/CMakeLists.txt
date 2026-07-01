add_library(note_project STATIC src/note_project.cpp)
target_include_directories(note_project PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(note_project PUBLIC tiny_json nfd)
