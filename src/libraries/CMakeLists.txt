# Tier 1 — Generic, application-independent utilities.
#
# Modules here could be lifted into any other project as-is. They have
# no knowledge of the application's domain and never depend on
# `core/`, `render/`, or `app/`.
#
# Each entry is a single line: the library directory under this folder
# owns its own `CMakeLists.txt`, public header `inc/<name>.hpp`, and
# optional `tests/` directory.

add_subdirectory(atomic_file)
add_subdirectory(json_io)
add_subdirectory(log)
add_subdirectory(logger)
add_subdirectory(process)
add_subdirectory(string_utils)
add_subdirectory(tiny_json)
