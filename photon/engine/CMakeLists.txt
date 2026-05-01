file(GLOB src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(engine ${src} include.hpp)

target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(engine
    PUBLIC
    gui
    gpu
    network
    parse
    synth
    KernelHeaders
    KernelAssets
    ModelHeaders
    ModelAssets
    DbcHeaders
    DbcAssets
)
