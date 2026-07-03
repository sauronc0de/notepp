# Tier 2 — Application Core.
#
# Modules here express the application's domain model, file IO,
# non-visual computation, and pure parsing logic. They may depend on
# `libraries/` but never on `render/` or `app/`.
#
# Each entry is a single line: the library directory under this folder
# owns its own `CMakeLists.txt`, public header `inc/<name>.hpp`, and
# optional `tests/` directory.

add_subdirectory(lang)
add_subdirectory(markdown_images)
add_subdirectory(markdown_sections)
add_subdirectory(markdown_tables)
add_subdirectory(note_history)
add_subdirectory(note_model)
add_subdirectory(note_project)
add_subdirectory(note_storage)
