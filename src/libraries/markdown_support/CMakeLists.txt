add_library(markdown_support STATIC src/markdown_support.cpp)
target_include_directories(markdown_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/app/inc
)
target_link_libraries(markdown_support PUBLIC string_utils mermaid_flowchart imgui nlohmann_json::nlohmann_json markdown_ui markdown_sections markdown_view)
