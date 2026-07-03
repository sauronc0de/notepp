add_library(markdown_images STATIC src/markdown_images.cpp)
target_include_directories(markdown_images PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(markdown_images PUBLIC string_utils)

if(ENABLE_TEST)
  add_executable(markdown_images_tests tests/markdown_images_tests.cpp)
  target_link_libraries(markdown_images_tests PRIVATE markdown_images)
  add_test(NAME markdown_images_tests COMMAND markdown_images_tests)
endif()