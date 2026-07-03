# Tier 4 — Executables.
#
# One subdirectory per executable. Each subdirectory defines exactly one
# target via `add_executable`, owns its own `main.cpp` and a
# `CMakeLists.txt`. Subdirectories may depend on every lower tier.

add_subdirectory(gui)
