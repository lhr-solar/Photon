add_library(utils_lib TelemetryData.cpp)

target_include_directories(utils_lib PUBLIC ${CMAKE_SOURCE_DIR})

# No external dependencies for now
