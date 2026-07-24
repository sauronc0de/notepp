add_library(terminal STATIC
  src/terminal.cpp
  src/pty_unix.cpp
  src/pty_windows.cpp
)

target_include_directories(terminal
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(terminal
  PUBLIC
    imgui
    vterm
)

if(UNIX AND NOT APPLE)
  # forkpty(3) lives in libutil on Linux. macOS has it in libSystem.
  target_link_libraries(terminal PUBLIC util)
endif()

if(ENABLE_TEST)
  add_executable(terminal_tests tests/terminal_tests.cpp)
  target_include_directories(terminal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(terminal_tests PRIVATE terminal)
  add_test(NAME terminal_tests COMMAND terminal_tests)
endif()
