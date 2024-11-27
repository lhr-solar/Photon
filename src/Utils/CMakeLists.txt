add_library(Utils STATIC
  Log.cpp
)

target_include_directories(Utils
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include/Utils
)

# No external dependencies for now
