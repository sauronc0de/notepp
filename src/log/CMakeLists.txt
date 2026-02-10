add_library(log STATIC src/log.cpp)
target_include_directories(log PUBLIC inc)
target_link_libraries(log PUBLIC logger)