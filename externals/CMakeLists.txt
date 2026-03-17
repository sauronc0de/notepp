# Find the SDL2 package
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)

# Find OpenGL
find_package(OpenGL REQUIRED)

# GLEW
find_package(GLEW REQUIRED)

#🌐 Add imgui library ---------------------------------------#
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9b-docking 
)

FetchContent_MakeAvailable(imgui)

# Add imgui library with SDL2 support
add_library(imgui 
    ${imgui_SOURCE_DIR}/imgui.cpp 
    ${imgui_SOURCE_DIR}/imgui_draw.cpp 
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp 
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp 
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 m)
# ------------------------------------------------------------#

# 🌐 Added ETL libraries -------------------------------------#
Include(FetchContent)
FetchContent_Declare(
  etl
  GIT_REPOSITORY https://github.com/ETLCPP/etl
  GIT_TAG        20.40.0
)
FetchContent_MakeAvailable(etl)
# ------------------------------------------------------------#


# 🌐 Added magic enum library -----------------------------------#
include(FetchContent)

FetchContent_Declare(
  magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG        v0.9.7
)

FetchContent_MakeAvailable(magic_enum)
# ------------------------------------------------------------#

# 🌐 Added boost library -----------------------------------#
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

# ------------------------------------------------------------#
# 🌐 Added json library -----------------------------------#
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3  # Or latest release
)

FetchContent_MakeAvailable(nlohmann_json)
# ------------------------------------------------------------#

# 🌐 Added sol library -----------------------------------#
# Fetch Sol2
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.0
)
FetchContent_MakeAvailable(sol2)
add_library(lua_common INTERFACE)
target_include_directories(lua_common INTERFACE ${LUA_INCLUDE_DIRS})
target_link_libraries(lua_common INTERFACE ${LUA_LIBRARIES} sol2::sol2)
# ------------------------------------------------------------#

# 🌐 Added sol library -----------------------------------#
# Fetch doctest
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11   # or a newer tag
)
FetchContent_MakeAvailable(doctest)

# ------------------------------------------------------------#

# ------------------------------------------------------------#
# 🌐 Added md4c (parser) -------------------------------------#
include(FetchContent)

FetchContent_Declare(
  md4c_fc
  GIT_REPOSITORY https://github.com/mity/md4c.git
  GIT_TAG        release-0.5.2
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c_fc)

set(MD4C_INCLUDE_DIR ${md4c_fc_SOURCE_DIR}/src)
# ------------------------------------------------------------#


# ------------------------------------------------------------#
# 🌐 Added imgui_md (renderer) -------------------------------#
FetchContent_Declare(
  imgui_md_fc
  GIT_REPOSITORY https://github.com/mekhontsev/imgui_md.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(imgui_md_fc)

add_library(imgui_md STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui_md/imgui_md.cpp
)

target_include_directories(imgui_md PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui_md
  ${MD4C_INCLUDE_DIR}          # <-- makes md4c.h visible
)

target_link_libraries(imgui_md PUBLIC imgui md4c)
# ------------------------------------------------------------#

# ------------------------------------------------------------#
# 🌐 Added Tree-sitter runtime + grammars --------------------#
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
  ${CMAKE_CURRENT_SOURCE_DIR}/tree_sitter_queries.hpp.in
  ${CMAKE_BINARY_DIR}/generated/tree_sitter_queries.hpp
  @ONLY
)

add_library(tree_sitter_queries INTERFACE)
target_include_directories(tree_sitter_queries INTERFACE ${CMAKE_BINARY_DIR}/generated)
# ------------------------------------------------------------#
