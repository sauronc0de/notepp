add_library(note_storage STATIC
  src/note_content_cache.cpp
  src/note_path.cpp
)
target_include_directories(note_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(note_storage PUBLIC string_utils)

if(ENABLE_TEST)
  add_executable(note_storage_tests tests/note_storage_tests.cpp)
  target_link_libraries(note_storage_tests PRIVATE note_storage)
  add_test(NAME note_storage_tests COMMAND note_storage_tests)
endif()