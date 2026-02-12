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
  ${imgui_md_fc_SOURCE_DIR}/imgui_md.cpp
)

target_include_directories(imgui_md PUBLIC
  ${imgui_md_fc_SOURCE_DIR}
  ${MD4C_INCLUDE_DIR}          # <-- makes md4c.h visible
)

target_link_libraries(imgui_md PUBLIC imgui md4c)
# ------------------------------------------------------------#
