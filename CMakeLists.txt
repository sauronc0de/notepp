cmake_minimum_required(VERSION 3.16)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(ProjectName "Notepp")
project(${ProjectName} VERSION 0.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  $<$<COMPILE_LANG_AND_ID:C,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:C,Clang>:-fcolor-diagnostics>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fcolor-diagnostics>
  $<$<COMPILE_LANGUAGE:C>:-Wall>
  $<$<COMPILE_LANGUAGE:CXX>:-Wall>
)

option(ENABLE_LOG "Enable logging" OFF)
option(ENABLE_RENDER "Enable rendering" ON)
option(ENABLE_TEST "Enable tests" OFF)

if(NOT NOTEPP_ASSETS_PATH)
  if(USE_PORTABLE_PATHS)
    set(NOTEPP_ASSETS_PATH "assets")
  else()
    message(STATUS "Use absolute development paths")
    set(NOTEPP_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
  endif()
endif()
if(NOT NOTEPP_DATA_PATH)
  if(USE_PORTABLE_PATHS)
    set(NOTEPP_DATA_PATH "data")
  else()
    set(NOTEPP_DATA_PATH "${CMAKE_SOURCE_DIR}/data")
  endif()
endif()

add_compile_definitions(
  ASSETS_PATH="${NOTEPP_ASSETS_PATH}"
  DATA_PATH="${NOTEPP_DATA_PATH}"
  IMGUI_USE_WCHAR32
  NOTEPP_VERSION="${PROJECT_VERSION}"
  $<$<CONFIG:Debug>:NOTEPP_DEBUG_UI>
  $<$<BOOL:${ENABLE_LOG}>:ENABLE_LOG=true>
  $<$<BOOL:${ENABLE_RENDER}>:ENABLE_RENDER=true>
  USE_PORTABLE_PATHS=$<BOOL:${USE_PORTABLE_PATHS}>
)

find_package(SDL2 REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/modules")
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PkgConfig REQUIRED)

add_subdirectory(externals)
add_subdirectory(libraries)
add_subdirectory(examples)
add_subdirectory(src)

set(NOTEPP_SOURCES
  src/main.cpp
  src/app.cpp
  src/markdown_sections.cpp
  src/markdown_view.cpp
  src/undo_redo.cpp
  src/project_manager.cpp
)

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

if(WIN32)
  enable_language(RC)

  list(APPEND NOTEPP_SOURCES
    ${CMAKE_SOURCE_DIR}/assets/icon/app.rc
  )

  add_executable(${PROJECT_NAME} WIN32 ${NOTEPP_SOURCES})
  target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
else()
  add_executable(${PROJECT_NAME} ${NOTEPP_SOURCES})
endif()

add_dependencies(${PROJECT_NAME} demo_note_content_gen)
target_include_directories(${PROJECT_NAME} PUBLIC src "${NOTEPP_GENERATED_DIR}")

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    log
    imgui
    imgui_md
    note_core
    tiny_json
    markdown_support
    markdown_ui
    markdown_code_highlight
    note_ui
    nfd
)

option(ENABLE_DEB_PACKAGE "Build Debian .deb package" OFF)

configure_file(
  ${CMAKE_SOURCE_DIR}/tools/installer/notepp.iss.in
  ${CMAKE_BINARY_DIR}/notepp.iss
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/tools/installer/notepp.desktop.in
  ${CMAKE_BINARY_DIR}/notepp.desktop
  @ONLY
)

if(WIN32)
  find_program(ISCC_EXE
    NAMES ISCC.exe
    PATHS "C:/Program Files (x86)/Inno Setup 6"
    REQUIRED
  )

  add_custom_target(installer ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CMAKE_SOURCE_DIR}/dist/Notepp"

    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:${PROJECT_NAME}>"
      "${CMAKE_SOURCE_DIR}/dist/Notepp/$<TARGET_FILE_NAME:${PROJECT_NAME}>"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/assets"
      "${CMAKE_SOURCE_DIR}/dist/Notepp/assets"

    COMMAND C:/msys64/usr/bin/bash.exe --login
      "${CMAKE_SOURCE_DIR}/tools/tasks/copy_mingw_dlls.sh"
      "${CMAKE_SOURCE_DIR}"
      "${CMAKE_SOURCE_DIR}/dist/Notepp/$<TARGET_FILE_NAME:${PROJECT_NAME}>"

    COMMAND "${ISCC_EXE}" "${CMAKE_BINARY_DIR}/notepp.iss"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Staging app files and building Inno Setup installer"
    VERBATIM
  )
elseif(ENABLE_DEB_PACKAGE)
  install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT notepp
  )

  install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION "share/notepp/assets"
    COMPONENT notepp
  )

  install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
    DESTINATION "share/notepp/data"
    COMPONENT notepp
  )

  install(FILES "${CMAKE_SOURCE_DIR}/readme.md"
    DESTINATION "share/doc/notepp"
    COMPONENT notepp
  )

  install(FILES "${CMAKE_BINARY_DIR}/notepp.desktop"
    DESTINATION "share/applications"
    COMPONENT notepp
  )

  set(CPACK_GENERATOR "DEB")
  set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/dist/${PROJECT_VERSION}")
  set(CPACK_PACKAGE_NAME "notepp")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_CONTACT "sauroncode@gmail.com")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "sauroncode <sauroncode@gmail.com>")
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Notepp - Markdown note-taking application")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0 (>= 2.0.0), libsdl2-image-2.0-0 (>= 2.0.0), libglew2.2 | libglew2.1, libgl1")
  set(CPACK_PACKAGE_FILE_NAME "notepp")
  set(CPACK_COMPONENTS_ALL "notepp")
  set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};notepp;/")

  include(CPack)

  add_custom_target(deb_package ALL
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building Debian package"
    VERBATIM
  )
endif()