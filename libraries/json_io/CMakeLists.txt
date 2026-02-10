add_library(json_io INTERFACE)
target_include_directories(json_io INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(json_io INTERFACE nlohmann_json boost_headers)
