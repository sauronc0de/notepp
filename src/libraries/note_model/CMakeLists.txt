add_library(note_model INTERFACE)
target_include_directories(note_model INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/inc
)
target_link_libraries(note_model INTERFACE imgui)

if(ENABLE_TEST)
  add_executable(note_model_tests tests/note_model_tests.cpp)
  target_link_libraries(note_model_tests PRIVATE note_model)
  add_test(NAME note_model_tests COMMAND note_model_tests)
endif()