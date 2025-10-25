
file(READ "${SPV_FILE}" HEX_CONTENT HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " HEX_ARRAY "${HEX_CONTENT}")
string(REGEX REPLACE ", $" "" HEX_ARRAY "${HEX_ARRAY}")
file(WRITE "${HEADER_FILE}" "#pragma once
#include <vector>
#include <cstdint>

inline std::vector<uint32_t> get_${SHADER_NAME}_spv() {
    static const uint32_t data[] = { ${HEX_ARRAY} };
    return std::vector<uint32_t>(data, data + sizeof(data) / sizeof(uint32_t));
}
")
