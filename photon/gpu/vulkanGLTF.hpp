#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
#include <string>
#include <vector>
#include "gpu.hpp"

enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };
struct Vertex{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
    glm::vec4 joint0;
    glm::vec4 weight0;
    glm::vec4 tangent;
    static VkVertexInputBindingDescription vertexInputBindingDescription;
    static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
    static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;

    static VkVertexInputBindingDescription inputBindingDescription(uint32_t binding);
    static VkVertexInputAttributeDescription inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component);
    static std::vector<VkVertexInputAttributeDescription> inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components);
    static VkPipelineVertexInputStateCreateInfo* getPipelineVertexInputState(const std::vector<VertexComponent> components);
};

struct Texture{
    VkImage image;
    VkImageLayout imageLayout;
    VkDeviceMemory deviceMemory;
    VkImageView view;
    uint32_t width, height;
    uint32_t mipLevels;
    uint32_t layerCount;
    VkDescriptorImageInfo descriptor;
    VkSampler sampler;
    uint32_t index;
};


enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
struct Material{
    AlphaMode alphaMode = ALPHAMODE_OPAQUE;
    float alphaCutoff = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    Texture* baseColorTexture = nullptr;
    Texture* metallicRoughnessTexture = nullptr;
    Texture* normalTexture = nullptr;
    Texture* occlusionTexture = nullptr;
    Texture* emissiveTexture = nullptr;

    Texture* specularGlossinessTexture;
    Texture* diffuseTexture;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    void createDescriptorSet(VkDevice logicalDevice, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags);
};

struct Primitive {
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t firstVertex;
    uint32_t vertexCount;
    Material& material;

    struct Dimensions {
        glm::vec3 min = glm::vec3(FLT_MAX);
        glm::vec3 max = glm::vec3(-FLT_MAX);
        glm::vec3 size;
        glm::vec3 center;
        float radius;
    } dimensions;

    void setDimensions(glm::vec3 min, glm::vec3 max);
    Primitive(uint32_t firstIndex, uint32_t indexCount, Material& material) : firstIndex(firstIndex), indexCount(indexCount), material(material) {};
};


struct Mesh{
    std::vector<Primitive*> primitives;
    std::string name;
    VulkanBuffer uniformBuffer;
    struct UniformBlock {
        glm::mat4 matrix;
        glm::mat4 jointMatrix[64]{};
        float jointcount{ 0 };
    } uniformBlock;

    Mesh(VulkanDevice vulkanDevice, glm::mat4 matrix);

};

struct Node{
    Node* parent;
    uint32_t index;
    std::vector<Node*> children;
    glm::mat4 matrix;
    std::string name;
    Mesh* mesh;
};

enum RenderFlags {
    BindImages = 0x00000001,
    RenderOpaqueNodes = 0x00000002,
    RenderAlphaMaskedNodes = 0x00000004,
    RenderAlphaBlendedNodes = 0x00000008
};

enum DescriptorBindingFlags { ImageBaseColor = 0x00000001, ImageNormalMap = 0x00000002 };

struct Model{
    bool bufferBound = false;
    struct Vertices{
        int count;
        VkBuffer buffer;
        VkDeviceMemory memory;
    } vertices;
    struct Indices{
        int count;
        VkBuffer buffer;
        VkDeviceMemory memory;
    } indices;
    std::vector<Node*> nodes;

    void draw(VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet);
    void drawNode(Node *node, VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet);
    void loadFromFile(std::string fileName, VulkanDevice* device, VkQueue transferQueue, uint32_t fileLoadingFlags, float scale);
};

enum FileLoadingFlags {
    None = 0x00000000,
    PreTransformVertices = 0x00000001,
    PreMultiplyVertexColors = 0x00000002,
    FlipY = 0x00000004,
    DontLoadImages = 0x00000008
};
