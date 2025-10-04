# photon/CMakeLists.txt
add_subdirectory(engine)
add_subdirectory(gpu)
add_subdirectory(gui)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(synth)

add_executable(Photon engine/main.cpp)

if (WIN32)
    target_sources(Photon PRIVATE ../fonts/Photon.rc)
endif()

target_link_libraries(Photon
    PRIVATE
    engine
)
