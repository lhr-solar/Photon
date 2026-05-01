#pragma once
#include <atomic>
#include "gpu.hpp"
#include "tiny_gltf.h"
#include <array>

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
       glm::vec3 target = {};
       float yaw = 0.0;
       float pitch = 0.0;
       float distance = 2.5f;
       float minDistance = 0.05f;
       float maxDistance = 8.0f;
       float orbitSensitivity = 0.35f;
       float panSensitivity = 1.0f;
       float zoomSensitivity = 0.08f;
};

struct gltfFrame {
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet frameDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet postDescriptorSet{VK_NULL_HANDLE};
    VkExtent2D extent{640, 640};
    VkBuffer uniformBuffer{VK_NULL_HANDLE};
    VkDeviceMemory uniformMemory{VK_NULL_HANDLE};
    void* uniformMapped{};
    VkImage sceneMsaaColorImage{VK_NULL_HANDLE};
    VkDeviceMemory sceneMsaaColorMemory{VK_NULL_HANDLE};
    VkImageView sceneMsaaColorView{VK_NULL_HANDLE};
    VkImage sceneColorImage{VK_NULL_HANDLE};
    VkDeviceMemory sceneColorMemory{VK_NULL_HANDLE};
    VkImageView sceneColorView{VK_NULL_HANDLE};
    VkImage sceneDepthImage{VK_NULL_HANDLE};
    VkDeviceMemory sceneDepthMemory{VK_NULL_HANDLE};
    VkImageView sceneDepthView{VK_NULL_HANDLE};
    VkImage outputImage{VK_NULL_HANDLE};
    VkDeviceMemory outputMemory{VK_NULL_HANDLE};
    VkImageView outputView{VK_NULL_HANDLE};
    VkFramebuffer sceneFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer postFramebuffer{VK_NULL_HANDLE};
    bool initialized{};
    unsigned long long texture{};
};

struct Gltf{
    GPU* gpu{};
    tinygltf::TinyGLTF loader{};
    tinygltf::Model model{};
    std::vector<unsigned char> source{};
    bool dirty{};
    std::atomic<bool> initialized{};
    std::atomic<bool> partInitialized{};
    std::vector<GltfVertex> vertices;
    std::vector<TextureResource> gltfTexturesSrgb;
    std::vector<TextureResource> gltfTexturesLinear;
    std::vector<MaterialRuntime> materials;
    std::vector<DrawItem> drawItems;
    Camera camera{};
    TextureResource fallbackWhiteTexture{};
    VkDevice device{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool internalDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout uniformDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout postDescriptorSetLayout{VK_NULL_HANDLE};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkRenderPass postRenderPass{VK_NULL_HANDLE};
    VkPipelineLayout gltfPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout postPipelineLayout{VK_NULL_HANDLE};
    VkPipeline gltfPipeline{VK_NULL_HANDLE};
    VkPipeline postPipeline{VK_NULL_HANDLE};
    VkShaderModule gltfFragModule{VK_NULL_HANDLE};
    VkShaderModule gltfVertModule{VK_NULL_HANDLE};
    VkShaderModule postFragModule{VK_NULL_HANDLE};
    VkShaderModule postVertModule{VK_NULL_HANDLE};
    const uint32_t* postFragmentShader{nullptr};
    size_t postFragmentShaderSize{};
    VkSampler offscreenColorSampler{VK_NULL_HANDLE};
    VkBuffer vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};
    std::vector<VkBuffer> materialUniformBuffers{};
    std::vector<VkDeviceMemory> materialUniformMemories{};
    std::vector<VkDescriptorSet> materialDescriptorSets{};
    VkVertexInputBindingDescription vertexBindingDescription{};
    std::array<VkVertexInputAttributeDescription, 4> vertexAttributeDescriptions{};
    VkFormat sceneColorFormat{VK_FORMAT_R8G8B8A8_UNORM};
    VkFormat sceneDepthFormat{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};
    std::vector<gltfFrame> frames{};
    uint32_t fif{};
    uint32_t* frameIndex{};

    void init(GPU& gpu, const unsigned char* newModel, size_t size,
        const uint32_t* fragmentShader = nullptr, size_t fragmentShaderSize = 0);
    void dispatchInit(GPU& gpu, const unsigned char* newModel, size_t size,
        const uint32_t* fragmentShader = nullptr, size_t fragmentShaderSize = 0);
    void prepareInit(GPU& gpu, const unsigned char* newModel, size_t size,
        const uint32_t* fragmentShader = nullptr, size_t fragmentShaderSize = 0);
    void finishInit(GPU& gpu);
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
