add_library(imgui_md STATIC
  src/imgui_md.cpp
)

target_include_directories(imgui_md PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/inc
  ${MD4C_INCLUDE_DIR}
)

target_link_libraries(imgui_md PUBLIC imgui md4c)
