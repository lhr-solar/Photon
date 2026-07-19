if(APPLE)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/gpuMetal.cpp")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/gpuMetal.cpp" PROPERTIES LANGUAGE OBJCXX)
else()
    file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    list(REMOVE_ITEM src "${CMAKE_CURRENT_SOURCE_DIR}/gpuMetal.cpp")
endif()

add_library(gpu ${src})

target_include_directories(gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gpu
    PUBLIC
    ${PHOTON_SDL_TARGET}
    imgui_runtime
    KernelHeaders
    KernelAssets
    EnvironmentHeaders
    EnvironmentAssets
    FontHeaders
    FontAssets
    Tracy::TracyClient
    photon_platform_graphics
)

if(APPLE)
    target_compile_options(gpu PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-weak>
    )
endif()
