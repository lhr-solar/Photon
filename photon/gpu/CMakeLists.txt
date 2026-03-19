file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(gpu ${src})

target_include_directories(gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gpu PUBLIC ${PHOTON_SDL_TARGET} gui)
add_dependencies(gpu GenerateKernelHeaders)

#target_link_libraries(this PUBLIC that)
