add_library(mermaid_flowchart STATIC ${PROJECT_SOURCE_DIR}/src/mermaid_flowchart.cpp)
target_include_directories(mermaid_flowchart PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mermaid_flowchart PUBLIC note_core imgui)

