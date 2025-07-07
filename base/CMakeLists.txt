#photon/base/CMakeLists.txt

file(GLOB BASE_SRC "*.cpp" "*.c" "*.hpp" "*.h" "../external/imgui/*.cpp")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

# Add generated Wayland protocol source explicitly
set(GENERATED_PROTOCOL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/xdg-shell-protocol.c")

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/vkloader.c)

add_library(base STATIC ${BASE_SRC} ${KTX_SOURCES} ${GENERATED_PROTOCOL_SRC})
if(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${WINLIBS})
 else(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)

message(STATUS "WAYLAND_CLIENT_LIBRARIES: ${WAYLAND_CLIENT_LIBRARIES}")
message(STATUS "WAYLAND_INCLUDE_DIR: ${WAYLAND_INCLUDE_DIR}")
