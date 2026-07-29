add_library(note_index STATIC src/note_index.cpp)
target_include_directories(note_index PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(note_index PUBLIC nlohmann_json::nlohmann_json)

if(ENABLE_TEST)
  add_executable(note_index_tests tests/note_index_tests.cpp)
  target_link_libraries(note_index_tests PRIVATE note_index)
  add_test(NAME note_index_tests COMMAND note_index_tests)
endif()
