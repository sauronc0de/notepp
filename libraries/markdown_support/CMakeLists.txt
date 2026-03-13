add_library(markdown_support STATIC markdown_support.cpp)
target_include_directories(markdown_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(markdown_support PUBLIC note_core mermaid_flowchart imgui)

