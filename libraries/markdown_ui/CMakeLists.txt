add_library(markdown_ui STATIC markdown_ui.cpp)
target_include_directories(markdown_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/libraries/markdown_support)
target_link_libraries(markdown_ui PUBLIC imgui note_core)
