add_library(mermaid STATIC
  src/mermaid.cpp
  src/mermaid_diagrams.cpp
  src/diagrams/sequence_parser.cpp
  src/diagrams/sequence_renderer.cpp
)
target_include_directories(mermaid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(mermaid PUBLIC string_utils imgui)
