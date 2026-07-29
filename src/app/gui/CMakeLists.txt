# Tier 4 executable: Notepp GUI.
#
# This subdirectory owns the `notepp` executable. It is the only place
# that defines an `add_executable()` target — every other tier only
# builds libraries. It links against `libraries/`, `core/`, and
# `render/` per the dependency matrix.

set(APP_SOURCES
  src/main.cpp
  src/app.cpp
  src/app_sdl.cpp
  src/app_imgui.cpp
  src/app_frame_limiter.cpp
  src/app_frame_end.cpp
  src/app_history_indicator.cpp
  src/app_profile_modal.cpp
)

# ── Generated header: demo note content (readme.md embedded into a header) ──
set(NOTEPP_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${NOTEPP_GENERATED_DIR}")

add_custom_command(
  OUTPUT "${NOTEPP_GENERATED_DIR}/demo_note_content.hpp"
  COMMAND ${CMAKE_COMMAND}
    -DINPUT_FILE=${CMAKE_SOURCE_DIR}/readme.md
    -DOUTPUT_FILE=${NOTEPP_GENERATED_DIR}/demo_note_content.hpp
    -P ${CMAKE_SOURCE_DIR}/cmake/embed_text_file.cmake
  DEPENDS "${CMAKE_SOURCE_DIR}/readme.md"
  COMMENT "Embedding readme.md -> generated/demo_note_content.hpp"
  VERBATIM
)

add_custom_target(demo_note_content_gen
  DEPENDS "${NOTEPP_GENERATED_DIR}/demo_note_content.hpp"
)

# ── Executable target ──────────────────────────────────────────────────────
if(WIN32)
  enable_language(RC)
  list(APPEND APP_SOURCES ${CMAKE_SOURCE_DIR}/assets/icon/app.rc)
  add_executable(${PROJECT_NAME} WIN32 ${APP_SOURCES})
  target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
else()
  add_executable(${PROJECT_NAME} ${APP_SOURCES})
endif()

add_dependencies(${PROJECT_NAME} demo_note_content_gen)

target_include_directories(${PROJECT_NAME}
  PRIVATE
    src
    inc
    "${NOTEPP_GENERATED_DIR}"
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    log
    imgui
    imgui_md
    string_utils
    note_history
    note_index
    lang
    emoji_picker
    note_model
    note_storage
    markdown_view
    markdown_sections
    note_project
    project_paths
    tiny_json
    markdown_support
    markdown_widgets
    markdown_code_highlight
    note_ui
    terminal
    nfd
)
