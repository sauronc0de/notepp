add_library(lang STATIC src/lang.cpp)
target_include_directories(lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(lang PUBLIC nlohmann_json::nlohmann_json)
