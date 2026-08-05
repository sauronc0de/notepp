add_executable(notepp-cli src/main.cpp)
target_compile_features(notepp-cli PRIVATE cxx_std_23)
target_link_libraries(notepp-cli PRIVATE command_ipc command_api note_project)
