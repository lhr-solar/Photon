include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

set(FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(FONT_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
set(FONT_OBJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/font_objects)

set(FONT_FILES
    ${FONT_DIR}/berk.ttf
    ${FONT_DIR}/openSans.ttf
    ${FONT_DIR}/sansFlex.ttf
    ${FONT_DIR}/HelveticaNeueMedium.otf
    ${FONT_DIR}/HelveticaNeueRoman.otf
    ${FONT_DIR}/HostGrotesk-Regular.ttf
    ${FONT_DIR}/Inter_28pt-Regular.ttf
    ${FONT_DIR}/Datatype-Regular.ttf
    ${FONT_DIR}/NotoSansSymbols-Regular.ttf
)

photon_embed_assets(
    HEADER_OUTPUT_DIR ${FONT_HEADER_OUTPUT_DIR}
    OBJECT_OUTPUT_DIR ${FONT_OBJECT_OUTPUT_DIR}
    HEADER_TARGET FontHeaders
    ASSET_TARGET FontAssets
    FILES ${FONT_FILES}
)
