#src/Application/CMakeLists.txt

add_library(application_lib Application.cpp)
target_include_directories(application_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
