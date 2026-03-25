# photon/engine/CMakeLists.txt
file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
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

add_dependencies(engine
    GenerateKernelHeaders
    GenerateKernelAssets
    GenerateModelHeaders
    GenerateModelAssets
    GenerateDbcHeaders
    GenerateDbcAssets
)
