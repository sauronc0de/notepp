add_library(mermaid STATIC
  src/mermaid.cpp
  src/mermaid_diagrams.cpp
  src/diagrams/sequence_parser.cpp
  src/diagrams/sequence_renderer.cpp
  src/diagrams/class_parser.cpp
  src/diagrams/class_renderer.cpp
  src/diagrams/state_parser.cpp
  src/diagrams/state_renderer.cpp
  src/diagrams/er_parser.cpp
  src/diagrams/er_renderer.cpp
  src/diagrams/journey_parser.cpp
  src/diagrams/journey_renderer.cpp
  src/diagrams/gantt_parser.cpp
  src/diagrams/gantt_renderer.cpp
)
target_include_directories(mermaid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(mermaid PUBLIC string_utils imgui)

if(ENABLE_TEST)
  add_subdirectory(tests)
endif()
