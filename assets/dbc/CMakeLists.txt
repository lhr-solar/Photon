include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

set(DBC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DBC_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
set(DBC_OBJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/dbc_objects)

file(GLOB DBC_FILES CONFIGURE_DEPENDS "${DBC_DIR}/*.dbc")
list(REMOVE_ITEM DBC_FILES
    "${DBC_DIR}/daybreak-master.dbc"
    "${DBC_DIR}/lonestar.dbc"
    "${DBC_DIR}/newDAQ.dbc"
    "${DBC_DIR}/test.dbc"
)

photon_embed_assets(
    HEADER_OUTPUT_DIR ${DBC_HEADER_OUTPUT_DIR}
    OBJECT_OUTPUT_DIR ${DBC_OBJECT_OUTPUT_DIR}
    HEADER_TARGET DbcHeaders
    ASSET_TARGET DbcAssets
    FILES ${DBC_FILES}
)
