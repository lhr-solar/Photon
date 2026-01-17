# photon/CMakeLists.txt
add_subdirectory(engine)
add_subdirectory(gpu)
add_subdirectory(gui)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(synth)

add_executable(Photon engine/main.cpp)

set(ENV_FILE "${CMAKE_SOURCE_DIR}/.env")
if (EXISTS "${ENV_FILE}")
    message(STATUS "[Photon] Found .env at ${ENV_FILE}")
    target_compile_definitions(Photon PRIVATE USE_DOTENV)
else()
    message(STATUS "[Photon] No .env found")
endif()

if (WIN32)
    target_sources(Photon PRIVATE ../fonts/Photon.rc)
endif()

target_link_libraries(Photon
    PRIVATE
    engine
)
