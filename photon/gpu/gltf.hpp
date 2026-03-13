#pragma once
#include "tiny_gltf.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "vulkanDevice.hpp"
#include <string>
#include <vector>

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

struct GltfModel{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    bool ready = false;
    bool dirty = false;
    bool initialized = false;
    VkExtent2D extent{640, 640};
    unsigned long long texture = 0;
    std::vector<unsigned char> sourceBytes;
    std::vector<GltfVertex> vertices;
    uint32_t width = 640;
    uint32_t height = 640;
    struct {
       glm::vec3 position = {};
       glm::vec3 front = {};
       glm::vec3 up = {};
       float yaw = 0.0;
       float pitch = 0.0;
    } camera;
    bool wasClicked = false;
    float jumpHeight = 0.0f;
    float jumpVelocity = 0.0f;

    VkDevice logicalDevice{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkQueue queue{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDescriptorPool internalDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet imguiDescriptorSet{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkRenderPass postRenderPass{VK_NULL_HANDLE};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkBuffer vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};

    VkVertexInputBindingDescription vBindingDescription{};
    std::vector<VkVertexInputAttributeDescription> vAttributeDescriptions;

    TextureResource fallbackWhiteTexture{};
    std::vector<TextureResource> gltfTexturesSrgb;
    std::vector<TextureResource> gltfTexturesLinear;
    std::vector<MaterialRuntime> materials;
    std::vector<DrawItem> drawItems;
    
    std::vector<VkBuffer> materialUniformBuffers;
    std::vector<VkDeviceMemory> materialUniformBufferMemory;
    VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout postDescriptorSetLayout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> materialDescriptorSets;
    VkDescriptorSet postDescriptorSet{VK_NULL_HANDLE};

    VkDescriptorSetLayout uniformDescriptorSetLayout{VK_NULL_HANDLE};
    VkViewport viewport{};
    VkRect2D scissor{};

    VkPipelineLayout gltfPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout postPipelineLayout{VK_NULL_HANDLE};
    VkPipeline gltfPipeline{VK_NULL_HANDLE};
    VkPipeline postPipeline{VK_NULL_HANDLE};

    VkShaderModule gltfFragModule{VK_NULL_HANDLE};
    VkShaderModule gltfVertModule{VK_NULL_HANDLE};
    VkShaderModule postFragModule{VK_NULL_HANDLE};
    VkShaderModule postVertModule{VK_NULL_HANDLE};

    VkImage sceneColorImage{VK_NULL_HANDLE};
    VkDeviceMemory sceneColorImageMemory{VK_NULL_HANDLE};
    VkImageView sceneColorImageView{VK_NULL_HANDLE};
    VkFormat sceneColorFormat{VK_FORMAT_R8G8B8A8_UNORM};
    VkFormat sceneDepthFormat{VK_FORMAT_UNDEFINED};
    VkImage sceneDepthImage{VK_NULL_HANDLE};
    VkDeviceMemory sceneDepthImageMemory{VK_NULL_HANDLE};
    VkImageView sceneDepthImageView{VK_NULL_HANDLE};
    VkImage outputImage{VK_NULL_HANDLE};
    VkDeviceMemory outputImageMemory{VK_NULL_HANDLE};
    VkImageView outputImageView{VK_NULL_HANDLE};
    VkFramebuffer sceneFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer postFramebuffer{VK_NULL_HANDLE};
    VkSampler offscreenColorSampler{VK_NULL_HANDLE};
    uint32_t frameCounter{0};

    std::vector<VkBuffer> frameUniformBuffers;
    std::vector<VkDeviceMemory> frameUniformBufferMemory;
    std::vector<void*> frameUniformMapped;
    std::vector<VkDescriptorSet> frameDescriptorSets;

    ~GltfModel() = default;
    void deleteGLTF();


    void initModel(const unsigned char* bytes, size_t length, const std::string& debugName, VkDevice LogicalDevice, VkQueue& Queue,
                   VkPhysicalDeviceMemoryProperties& memoryProperties, VkCommandPool& commandPool, VkExtent2D extent);
    void createResources(VulkanDevice vulkanDevice, VkExtent2D extent, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);
    void destroyResources(bool releaseDescriptor, VkDevice deviceHandle, VkDescriptorPool descriptorPool);
    void updateBuffers(uint32_t frameIndex, const GltfMVP& mvp);
    void recordShaderPass(VkCommandBuffer commandBuffer);
    void initFrameResources(uint32_t frameCount);
    void createImages();
    void initShaders();
    void buildGltfPipeline();
    void buildPostPipeline();
    void createVertexBuffer();
    TextureResource createTexture2DFromRGBA(const unsigned char* rgba, uint32_t width, uint32_t height, VkFormat format);
    void loadGltfTextures();
    void destroyTexture(TextureResource& texture);
    void createFallbackTexture();
    void createMaterialResources();
    glm::mat4 nodeLocalMatrix(const tinygltf::Node& node);
    void appendNodeMesh(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix, std::vector<GltfVertex>& outVertices,
                        std::vector<PrimitiveRange>& outRanges);
    void appendPrimitiveVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4& worldMatrix, 
                                 std::vector<GltfVertex>& outVertices, std::vector<PrimitiveRange>& outRanges);
};
