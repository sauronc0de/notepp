add_library(sync_coordinator STATIC src/sync_coordinator.cpp)
target_include_directories(sync_coordinator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

if(ENABLE_TEST)
  add_executable(sync_coordinator_tests tests/sync_coordinator_tests.cpp)
  target_link_libraries(sync_coordinator_tests PRIVATE sync_coordinator)
  add_test(NAME sync_coordinator_tests COMMAND sync_coordinator_tests)
endif()
