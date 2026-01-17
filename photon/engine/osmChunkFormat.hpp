#pragma once

#include <cstdint>

// Simple binary file header for OSM chunk meshes.
// Payload:
// - Header
// - vertex[vertexCount]
// - uint32_t[indexCount]
struct OSMChunkFileHeader {
    uint8_t magic[4]; // 'O','S','M','C'
    int32_t chunkX;
    int32_t chunkZ;
    uint32_t vertexCount;
    uint32_t indexCount;
};
