add_library(markdown_model STATIC src/markdown_sections.cpp)
target_include_directories(markdown_model
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/app/inc
)
target_link_libraries(markdown_model PUBLIC note_core)
