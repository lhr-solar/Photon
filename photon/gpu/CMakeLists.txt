file(GLOB src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(gpu ${src})

target_include_directories(gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gpu
    PUBLIC
    ${PHOTON_SDL_TARGET}
    imgui_runtime
    KernelHeaders
    KernelAssets
    FontHeaders
    FontAssets
    Tracy::TracyClient
)
