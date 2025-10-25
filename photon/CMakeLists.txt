# ------------------------------
# Subdirectories
# ------------------------------
add_subdirectory(engine)
add_subdirectory(gpu)
add_subdirectory(gui)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(synth)

# ------------------------------
# Photon executable
# ------------------------------
add_executable(Photon engine/main.cpp)

if (WIN32)
    target_sources(Photon PRIVATE ../fonts/Photon.rc)
endif()

# ------------------------------
# Link internal libraries
# ------------------------------
target_link_libraries(Photon
    PRIVATE
        engine
)

# ------------------------------
# Link FFmpeg libraries
# ------------------------------
if(DEFINED FFMPEG_LIBRARIES AND FFMPEG_LIBRARIES)
    target_link_libraries(Photon
        PRIVATE
            ${FFMPEG_LIBRARIES}
    )
endif()

# ------------------------------
# Include FFmpeg headers
# ------------------------------
if(DEFINED FFMPEG_INCLUDE_DIR AND FFMPEG_INCLUDE_DIR)
    target_include_directories(Photon
        PRIVATE
            ${FFMPEG_INCLUDE_DIR}
    )
endif()
