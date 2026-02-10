# FindSDL2_image.cmake
#
# Locate SDL2_image library
# This module defines
#  SDL2_IMAGE_INCLUDE_DIRS, where to find SDL_image.h
#  SDL2_IMAGE_LIBRARIES, the libraries needed to use SDL2_image
#  SDL2_IMAGE_FOUND, If false, do not try to use SDL2_image.

find_package(PkgConfig)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PKG_SDL2_IMAGE QUIET SDL2_image)
endif()

find_path(SDL2_IMAGE_INCLUDE_DIR
    NAMES SDL_image.h
    PATHS
        ${PKG_SDL2_IMAGE_INCLUDE_DIRS}
        /usr/include/SDL2
        /usr/local/include/SDL2
)

find_library(SDL2_IMAGE_LIBRARY
    NAMES SDL2_image
    PATHS
        ${PKG_SDL2_IMAGE_LIBRARY_DIRS}
        /usr/lib
        /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2_image
    REQUIRED_VARS SDL2_IMAGE_LIBRARY SDL2_IMAGE_INCLUDE_DIR
)

if(SDL2_IMAGE_FOUND)
    set(SDL2_IMAGE_LIBRARIES ${SDL2_IMAGE_LIBRARY})
    set(SDL2_IMAGE_INCLUDE_DIRS ${SDL2_IMAGE_INCLUDE_DIR})

    if(NOT TARGET SDL2_image::SDL2_image)
        add_library(SDL2_image::SDL2_image UNKNOWN IMPORTED)
        set_target_properties(SDL2_image::SDL2_image PROPERTIES
            IMPORTED_LOCATION "${SDL2_IMAGE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_IMAGE_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(SDL2_IMAGE_LIBRARY SDL2_IMAGE_INCLUDE_DIR)
