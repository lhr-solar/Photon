#pragma once
#include "gpu.hpp"
#include "tiny_gltf.h"

struct alignas(16) GltfMVP {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camPos;
};

struct GltfVertex {
    float pos[3];
    float color[3];
    float uv[2];
    float normal[3];
};

struct GltfPushConstants{
    float resolution[2];
    float time;
    float _pad;
};

struct alignas(16) MaterialParams {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float normalScale{1.0f};
    float occlusionStrength{1.0f};
    float alphaCutoff{0.5f};
    int alphaMode{0}; // 0: OPAQUE, 1: MASK, 2: BLEND
    int hasBaseColorTexture{0};
    int hasMetallicRoughnessTexture{0};
    int hasNormalTexture{0};
    int hasOcclusionTexture{0};
    int hasEmissiveTexture{0};
};

struct PrimitiveRange {
    uint32_t firstVertex{0};
    uint32_t vertexCount{0};
    int materialIndex{-1};
};

struct TextureResource {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};
};

struct MaterialRuntime {
    MaterialParams params{};
    int baseColorTextureIndex{-1};
    int metallicRoughnessTextureIndex{-1};
    int normalTextureIndex{-1};
    int occlusionTextureIndex{-1};
    int emissiveTextureIndex{-1};
};

struct DrawItem {
    uint32_t firstVertex{0};
    uint32_t vertexCount{0};
    uint32_t materialIndex{0};
};

struct Camera {
       glm::vec3 position = {};
       glm::vec3 front = {};
       glm::vec3 up = {};
       float yaw = 0.0;
       float pitch = 0.0;
};


struct Gltf{
    tinygltf::TinyGLTF loader{};
    tinygltf::Model model{};
    std::vector<unsigned char> source{};
    bool dirty{};
    std::vector<GltfVertex> vertices;
    std::vector<TextureResource> gltfTexturesSrgb;
    std::vector<TextureResource> gltfTexturesLinear;
    std::vector<MaterialRuntime> materials;
    std::vector<DrawItem> drawItems;
    Camera camera{};

    void init(GPU& gpu, const unsigned char* newModel, size_t size);
    void render(GPU& gpu, VkCommandBuffer& commandBuffer);
    void rebuild(GPU& gpu);
    void destroy();

    void appendNodeMesh(const tinygltf::Model& model, int nodeIndex, 
        const glm::mat4& parentMatrix,  std::vector<GltfVertex>& outVertices, 
        std::vector<PrimitiveRange>& outRanges);
    void appendPrimitiveVertices(const tinygltf::Model& model, 
        const tinygltf::Primitive& primitive, const glm::mat4& worldMatrix,
        std::vector<GltfVertex>& outVertices, std::vector<PrimitiveRange>& outRanges);
    glm::mat4 nodeLocalMatrix(const tinygltf::Node& node);
};
