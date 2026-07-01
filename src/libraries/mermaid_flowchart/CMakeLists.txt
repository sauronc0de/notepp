add_library(mermaid_flowchart STATIC
  src/mermaid_flowchart.cpp
  src/mermaid_diagrams.cpp
)
target_include_directories(mermaid_flowchart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(mermaid_flowchart PUBLIC string_utils imgui)
