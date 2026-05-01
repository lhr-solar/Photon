file(GLOB src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(synth ${src})

target_include_directories(synth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
