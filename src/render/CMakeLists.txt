# Tier 3 — Visual / UI / Graphics.
#
# Modules here own widget rendering, theme handling, image loading for
# display, font rendering, and any code that touches a graphics
# toolkit. They may depend on `libraries/` and `core/` but never on `app/`.
#
# Each entry is a single line: the library directory under this folder
# owns its own `CMakeLists.txt`, public header `inc/<name>.hpp`, and
# optional `tests/` directory.

add_subdirectory(emoji_picker)
add_subdirectory(imgui_md)
add_subdirectory(markdown_code_highlight)
add_subdirectory(markdown_editor)
add_subdirectory(markdown_support)
add_subdirectory(markdown_view)
add_subdirectory(markdown_widgets)
add_subdirectory(mermaid)
add_subdirectory(note_ui)
