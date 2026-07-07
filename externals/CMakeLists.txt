# All third-party dependencies live here. See docs/cmake_guidelines.md §9.
# Each FetchContent / find_package block is independent; targets are
# scoped below the file with `-w` so the project's `-Werror` does not
# fail on upstream warnings.

# Find the SDL2 package
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)

# Find OpenGL
find_package(OpenGL REQUIRED)

# GLEW
find_package(GLEW REQUIRED)

# ── ImGui library ────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9b-docking
)
FetchContent_MakeAvailable(imgui)

# ── FreeType (system preferred, source fallback for Windows CI) ──────────
find_package(Freetype QUIET)
if(NOT Freetype_FOUND)
  FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
  )
  set(FT_DISABLE_ZLIB    TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2   TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG     TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI  TRUE CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(freetype)
  set(FREETYPE_LIBRARIES freetype)
  set(FREETYPE_INCLUDE_DIRS ${freetype_SOURCE_DIR}/include)
endif()

# Build imgui with SDL2 + FreeType support
add_library(imgui
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/freetype
    ${FREETYPE_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC SDL2::SDL2 m ${FREETYPE_LIBRARIES})

# ── magic_enum ───────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG        v0.9.7
)
FetchContent_MakeAvailable(magic_enum)

# ── boost (header-only hana + preprocessor) ──────────────────────────────
include(FetchContent)
FetchContent_Declare(
  Boost
  GIT_REPOSITORY https://github.com/boostorg/boost.git
  GIT_TAG boost-1.81.0
  GIT_SUBMODULES "libs/hana;libs/preprocessor"
  GIT_SUBMODULES_RECURSE TRUE
  GIT_SHALLOW FALSE
)
FetchContent_Populate(Boost)
add_library(boost_headers INTERFACE)
target_include_directories(boost_headers INTERFACE
    ${boost_SOURCE_DIR}/libs/hana/include
    ${boost_SOURCE_DIR}/libs/preprocessor/include
)

# ── nlohmann_json ────────────────────────────────────────────────────────
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── md4c (markdown parser) ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  md4c_fc
  GIT_REPOSITORY https://github.com/mity/md4c.git
  GIT_TAG        release-0.5.2
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c_fc)
set(MD4C_INCLUDE_DIR ${md4c_fc_SOURCE_DIR}/src)
set(MD4C_INCLUDE_DIR ${MD4C_INCLUDE_DIR} PARENT_SCOPE)

# ── Tree-sitter runtime + grammars ───────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  tree_sitter_fc
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG        master
)
FetchContent_GetProperties(tree_sitter_fc)
if(NOT tree_sitter_fc_POPULATED)
  FetchContent_Populate(tree_sitter_fc)
endif()

add_library(tree_sitter_runtime STATIC
  ${tree_sitter_fc_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter_runtime
  PUBLIC ${tree_sitter_fc_SOURCE_DIR}/lib/include
  PRIVATE ${tree_sitter_fc_SOURCE_DIR}/lib/src
)

function(notepp_add_tree_sitter_grammar target_name)
  set(options)
  set(oneValueArgs SOURCE_DIR)
  cmake_parse_arguments(GRAMMAR "${options}" "${oneValueArgs}" "" ${ARGN})

  add_library(${target_name} STATIC
    ${GRAMMAR_SOURCE_DIR}/src/parser.c
  )

  if(EXISTS "${GRAMMAR_SOURCE_DIR}/src/scanner.c")
    target_sources(${target_name} PRIVATE ${GRAMMAR_SOURCE_DIR}/src/scanner.c)
  endif()

  if(EXISTS "${GRAMMAR_SOURCE_DIR}/src/scanner.cc")
    target_sources(${target_name} PRIVATE ${GRAMMAR_SOURCE_DIR}/src/scanner.cc)
  endif()

  target_link_libraries(${target_name} PUBLIC tree_sitter_runtime)
  target_include_directories(${target_name} PRIVATE ${GRAMMAR_SOURCE_DIR}/src)
endfunction()

FetchContent_Declare(
  tree_sitter_c_fc
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
  GIT_TAG        master
)
FetchContent_GetProperties(tree_sitter_c_fc)
if(NOT tree_sitter_c_fc_POPULATED)
  FetchContent_Populate(tree_sitter_c_fc)
endif()
notepp_add_tree_sitter_grammar(tree_sitter_c_grammar SOURCE_DIR ${tree_sitter_c_fc_SOURCE_DIR})

FetchContent_Declare(
  tree_sitter_cpp_fc
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
  GIT_TAG        master
)
FetchContent_GetProperties(tree_sitter_cpp_fc)
if(NOT tree_sitter_cpp_fc_POPULATED)
  FetchContent_Populate(tree_sitter_cpp_fc)
endif()
notepp_add_tree_sitter_grammar(tree_sitter_cpp_grammar SOURCE_DIR ${tree_sitter_cpp_fc_SOURCE_DIR})

FetchContent_Declare(
  tree_sitter_python_fc
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
  GIT_TAG        master
)
FetchContent_GetProperties(tree_sitter_python_fc)
if(NOT tree_sitter_python_fc_POPULATED)
  FetchContent_Populate(tree_sitter_python_fc)
endif()
notepp_add_tree_sitter_grammar(tree_sitter_python_grammar SOURCE_DIR ${tree_sitter_python_fc_SOURCE_DIR})

FetchContent_Declare(
  tree_sitter_bash_fc
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-bash.git
  GIT_TAG        master
)
FetchContent_GetProperties(tree_sitter_bash_fc)
if(NOT tree_sitter_bash_fc_POPULATED)
  FetchContent_Populate(tree_sitter_bash_fc)
endif()
notepp_add_tree_sitter_grammar(tree_sitter_bash_grammar SOURCE_DIR ${tree_sitter_bash_fc_SOURCE_DIR})

file(READ "${tree_sitter_c_fc_SOURCE_DIR}/queries/highlights.scm" TREE_SITTER_C_HIGHLIGHTS_QUERY)
file(READ "${tree_sitter_cpp_fc_SOURCE_DIR}/queries/highlights.scm" TREE_SITTER_CPP_EXTENSION_HIGHLIGHTS_QUERY)
set(TREE_SITTER_CPP_HIGHLIGHTS_QUERY "${TREE_SITTER_C_HIGHLIGHTS_QUERY}\n${TREE_SITTER_CPP_EXTENSION_HIGHLIGHTS_QUERY}")
file(READ "${tree_sitter_python_fc_SOURCE_DIR}/queries/highlights.scm" TREE_SITTER_PYTHON_HIGHLIGHTS_QUERY)
file(READ "${tree_sitter_bash_fc_SOURCE_DIR}/queries/highlights.scm" TREE_SITTER_BASH_HIGHLIGHTS_QUERY)

configure_file(
  ${PROJECT_SOURCE_DIR}/src/render/markdown_code_highlight/tree_sitter_queries.hpp.in
  ${CMAKE_BINARY_DIR}/generated/tree_sitter_queries.hpp
  @ONLY
)

add_library(tree_sitter_queries INTERFACE)
target_include_directories(tree_sitter_queries INTERFACE ${CMAKE_BINARY_DIR}/generated)

# ── Native File Dialog Extended ──────────────────────────────────────────
FetchContent_Declare(
  nativefiledialog
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG master
)
FetchContent_MakeAvailable(nativefiledialog)
FetchContent_GetProperties(nativefiledialog)

set(NFD_INCLUDE_DIR "${nativefiledialog_SOURCE_DIR}/src/include")

if(TARGET nfd)
  message(STATUS "Patching nfd include dir: ${NFD_INCLUDE_DIR}")
  target_include_directories(nfd
    PUBLIC
      "${NFD_INCLUDE_DIR}"
  )
endif()

# ─────────────────────────────────────────────────────────────────────────
#  Third-party warning suppression
# ─────────────────────────────────────────────────────────────────────────
# The top-level project enables `-Werror` so that warnings in our own
# code become build errors. Upstream code, however, is outside our
# control — a warning that surfaces today in a FetchContent dependency
# would otherwise break the develop_gui / Test presets on every clean
# build. We opt each FetchContent-produced target out of warnings with
# a PRIVATE `-w`. The project's own code is unaffected.
set(_NOTEPP_THIRD_PARTY_TARGETS
  freetype
  imgui
  boost_headers
  md4c
  tree_sitter_runtime
  tree_sitter_c_grammar
  tree_sitter_cpp_grammar
  tree_sitter_python_grammar
  tree_sitter_bash_grammar
  nfd
)
foreach(_tgt IN LISTS _NOTEPP_THIRD_PARTY_TARGETS)
  if(TARGET ${_tgt})
    get_target_property(_tgt_type ${_tgt} TYPE)
    if(_tgt_type STREQUAL "INTERFACE" OR _tgt_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${_tgt} INTERFACE -w)
    else()
      target_compile_options(${_tgt} PRIVATE -w)
    endif()
  endif()
endforeach()
