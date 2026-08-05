add_library(command_ipc STATIC src/command_ipc.cpp)
target_include_directories(command_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(command_ipc PUBLIC command_api)
