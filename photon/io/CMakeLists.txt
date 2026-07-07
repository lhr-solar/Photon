set(io_src
    csv_exporter.cpp
    replay_controller.cpp
)

# network_suspender.cpp and pre_fault_recorder.cpp are only compiled for the
# full Photon build (not the dashboard-only target).
if (NOT PHOTON_DASHBOARD_ONLY)
    list(APPEND io_src network_suspender.cpp)
    list(APPEND io_src pre_fault_recorder.cpp)
endif()

find_package(Threads REQUIRED)

add_library(io ${io_src})
target_include_directories(io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(io PUBLIC parse Threads::Threads)

if (PHOTON_DASHBOARD_ONLY)
    target_compile_definitions(io PUBLIC PHOTON_DASHBOARD_ONLY)
endif()
