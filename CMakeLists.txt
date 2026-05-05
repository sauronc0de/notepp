cmake_minimum_required(VERSION 3.16)

# CMake 4 removed compatibility with policy versions older than 3.5.
# Some fetched third-party dependencies still declare older minimums.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(ProjectName "Notepp")
project(${ProjectName} VERSION 0.0.1 LANGUAGES C CXX)

# C++ standard (target-level "features" would also work, but this is fine)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Colorize diagnostics under Ninja (or any generator)
add_compile_options(
  $<$<COMPILE_LANG_AND_ID:C,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:C,Clang>:-fcolor-diagnostics>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fcolor-diagnostics>
)

# Warnings (per language)
add_compile_options(
  $<$<COMPILE_LANGUAGE:C>:-Wall>
  $<$<COMPILE_LANGUAGE:CXX>:-Wall>
)

# Export compile commands (also enabled in presets; harmless to keep)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Feature toggles coming from presets; provide defaults for non-preset runs
option(ENABLE_LOG "Enable logging" OFF)
option(ENABLE_RENDER "Enable rendering" ON)
option(ENABLE_TEST "Enable tests" OFF)

# Use paths relative to executable/install folder instead of absolute development paths
if(USE_PORTABLE_PATHS)
  message(STATUS "Use paths relative to executable/install folder")
  set(NOTEPP_ASSETS_PATH "assets")
  set(NOTEPP_DATA_PATH "data")
else()
  message(STATUS "Use absolute development paths")
  set(NOTEPP_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
  set(NOTEPP_DATA_PATH "${CMAKE_SOURCE_DIR}/data")
endif()

# Project-wide compile definitions (directory scope)
add_compile_definitions(
  ASSETS_PATH="${NOTEPP_ASSETS_PATH}"
  DATA_PATH="${NOTEPP_DATA_PATH}"
  IMGUI_USE_WCHAR32
  $<$<CONFIG:Debug>:NOTEPP_DEBUG_UI>
  $<$<BOOL:${ENABLE_LOG}>:ENABLE_LOG=true>
  $<$<BOOL:${ENABLE_RENDER}>:ENABLE_RENDER=true>
)
# ------------------------------------------------------------
# Dependencies
find_package(SDL2 REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/modules")
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED lua5.4)

# ------------------------------------------------------------
# Subdirs
add_subdirectory(externals)
add_subdirectory(libraries)
add_subdirectory(examples)
add_subdirectory(src)

add_executable(${PROJECT_NAME} src/main.cpp src/app.cpp src/markdown_sections.cpp src/markdown_view.cpp src/undo_redo.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC src)
target_link_libraries(${PROJECT_NAME} PRIVATE log imgui imgui_md note_core tiny_json markdown_support markdown_ui markdown_code_highlight note_ui)

# Installer
configure_file(
  ${CMAKE_SOURCE_DIR}/tools/installer/notepp.iss.in
  ${CMAKE_BINARY_DIR}/notepp.iss
  @ONLY
)