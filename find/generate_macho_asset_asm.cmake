if (NOT DEFINED PHOTON_EMBED_ASSET_INPUT OR
    NOT DEFINED PHOTON_EMBED_ASSET_OUTPUT OR
    NOT DEFINED PHOTON_EMBED_ASSET_SYMBOL)
    message(FATAL_ERROR "generate_macho_asset_asm.cmake requires input, output, and symbol variables")
endif()

file(TO_CMAKE_PATH "${PHOTON_EMBED_ASSET_INPUT}" _photon_asset_input)
string(REPLACE "\"" "\\\"" _photon_asset_input "${_photon_asset_input}")

file(WRITE "${PHOTON_EMBED_ASSET_OUTPUT}"
".section __DATA,__const
.p2align 4
.globl __binary_${PHOTON_EMBED_ASSET_SYMBOL}_start
__binary_${PHOTON_EMBED_ASSET_SYMBOL}_start:
.incbin \"${_photon_asset_input}\"
.globl __binary_${PHOTON_EMBED_ASSET_SYMBOL}_end
__binary_${PHOTON_EMBED_ASSET_SYMBOL}_end:
")
