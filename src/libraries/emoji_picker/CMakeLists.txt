add_library(emoji_picker STATIC src/emoji_picker.cpp)
target_include_directories(emoji_picker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(emoji_picker PUBLIC imgui)
