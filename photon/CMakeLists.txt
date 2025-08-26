add_subdirectory(engine)
add_subdirectory(gpu)
add_subdirectory(gui)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(synth)

add_executable(Photon engine/main.cpp)

target_link_libraries(Photon
    PRIVATE
    engine
)
