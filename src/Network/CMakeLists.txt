add_library(Network STATIC
  AsioNetwork.cpp
)

target_include_directories(Network
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include/Network
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(Network
  PRIVATE
    Boost::system
)
