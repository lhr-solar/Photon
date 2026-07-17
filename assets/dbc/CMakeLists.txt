include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

set(DBC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DBC_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
set(DBC_OBJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/dbc_objects)

file(GLOB DBC_FILES CONFIGURE_DEPENDS "${DBC_DIR}/*.dbc")

# The car-facing HighNoon bundle is generated from the pinned Embedded
# Sharepoint submodule.  Keep the source files separate so collision validation
# can fail the build instead of relying on a DBC parser's last-file-wins rule.
set(HIGHNOON_DBC_DIR "${DBC_DIR}/Embedded-Sharepoint/can/dbc/HighNoon")
set(HIGHNOON_TEAM_DBC "${CMAKE_CURRENT_BINARY_DIR}/highnoon_team.dbc")
set(HIGHNOON_AWS_DBC "${CMAKE_CURRENT_BINARY_DIR}/highnoon_aws.dbc")
set(HIGHNOON_TEAM_SOURCES
    "${HIGHNOON_DBC_DIR}/CarCAN.dbc"
    "${HIGHNOON_DBC_DIR}/BPSCAN.dbc"
    "${HIGHNOON_DBC_DIR}/MotorCAN.dbc"
    "${HIGHNOON_DBC_DIR}/SteeringCAN.dbc"
    "${HIGHNOON_DBC_DIR}/DAqCAN.dbc"
)
foreach(_highnoon_dbc IN LISTS HIGHNOON_TEAM_SOURCES)
    if(NOT EXISTS "${_highnoon_dbc}")
        message(FATAL_ERROR
            "Missing ${_highnoon_dbc}. Run: git submodule update --init --recursive")
    endif()
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/merge_highnoon_dbc.py"
            "${HIGHNOON_TEAM_DBC}" ${HIGHNOON_TEAM_SOURCES}
    RESULT_VARIABLE HIGHNOON_DBC_RESULT
    OUTPUT_VARIABLE HIGHNOON_DBC_OUTPUT
    ERROR_VARIABLE HIGHNOON_DBC_ERROR
)
if(NOT HIGHNOON_DBC_RESULT EQUAL 0)
    message(FATAL_ERROR "HighNoon DBC validation failed:\n${HIGHNOON_DBC_OUTPUT}${HIGHNOON_DBC_ERROR}")
endif()
list(APPEND DBC_FILES "${HIGHNOON_TEAM_DBC}")

# AWS carries the regular HighNoon frames plus its telemetry-only messages
# encoded in the extended DBC namespace (0x1000..0x17FF).  Keep that source
# selectable independently of direct PCAN so those IDs are never put on the
# physical standard-CAN bus.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/merge_highnoon_dbc.py"
            "${HIGHNOON_AWS_DBC}" ${HIGHNOON_TEAM_SOURCES} "${DBC_DIR}/telem_rev1.dbc"
    RESULT_VARIABLE HIGHNOON_AWS_DBC_RESULT
    OUTPUT_VARIABLE HIGHNOON_AWS_DBC_OUTPUT
    ERROR_VARIABLE HIGHNOON_AWS_DBC_ERROR
)
if(NOT HIGHNOON_AWS_DBC_RESULT EQUAL 0)
    message(FATAL_ERROR "HighNoon AWS DBC validation failed:\n${HIGHNOON_AWS_DBC_OUTPUT}${HIGHNOON_AWS_DBC_ERROR}")
endif()
list(APPEND DBC_FILES "${HIGHNOON_AWS_DBC}")

photon_embed_assets(
    HEADER_OUTPUT_DIR ${DBC_HEADER_OUTPUT_DIR}
    OBJECT_OUTPUT_DIR ${DBC_OBJECT_OUTPUT_DIR}
    HEADER_TARGET DbcHeaders
    ASSET_TARGET DbcAssets
    FILES ${DBC_FILES}
)
