# photon/engine/CMakeLists.txt
file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(engine ${src} include.hpp)

target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(engine
    PUBLIC
    gpu
    gui
    network
    parse
    synth
)
