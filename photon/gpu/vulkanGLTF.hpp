#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

// Enable GLM hashing for vector/matrix types
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "vulkanDevice.hpp"
#include "vulkanBuffer.hpp"

#define VK_CHECK(x)                                                                                              \
    do                                                                                                           \
    {                                                                                                            \
        VkResult err = x;                                                                                        \
        if (err)                                                                                                 \
        {                                                                                                        \
            std::cout << "Detected Vulkan error: " << err << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            abort();                                                                                             \
        }                                                                                                        \
    } while (0)

namespace tinygltf
{
    class Model;
    struct Primitive;
    struct Node;
}

struct TextureGPU
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
};

// mesh info
struct Primitive
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t materialIndex = -1;
    // per-material texture binding
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

// vertex layout
enum class VertexComponent
{
    Position,
    Normal,
    UV,
    Color,
    Tangent,
    Joint0,
    Weight0
};
struct alignas(16) ShaderMeshData
{
    glm::mat4 matrix;
    glm::mat4 jointMatrix[128]{};
    uint32_t jointcount{0};
};

struct vertex
{
    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::vec4 color{1.0f};
    glm::vec4 joint0{};
    glm::vec4 weight0{};
    glm::vec4 tangent{};

    bool operator==(const vertex &other) const noexcept
    {
        return pos == other.pos &&
               normal == other.normal &&
               uv == other.uv &&
               color == other.color &&
               joint0 == other.joint0 &&
               weight0 == other.weight0 &&
               tangent == other.tangent;
    }

    static VkVertexInputBindingDescription vertexInputBindingDescription;
    static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
    static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;

    static VkVertexInputBindingDescription inputBindingDescription(uint32_t binding);
    static VkVertexInputAttributeDescription inputAttributeDescription(
        uint32_t binding, uint32_t location, VertexComponent component);
    static std::vector<VkVertexInputAttributeDescription> inputAttributeDescriptions(
        uint32_t binding, const std::vector<VertexComponent> components);
    static VkPipelineVertexInputStateCreateInfo *getPipelineVertexInputState(
        const std::vector<VertexComponent> components);
};

namespace std
{
    template <>
    struct hash<vertex>
    {
        size_t operator()(const vertex &v) const noexcept
        {
            size_t seed = 0u;
            auto hash_combine = [&seed](auto const &val)
            {
                seed ^= std::hash<std::decay_t<decltype(val)>>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            hash_combine(v.pos);
            hash_combine(v.normal);
            hash_combine(v.uv);
            hash_combine(v.color);
            hash_combine(v.joint0);
            hash_combine(v.weight0);
            hash_combine(v.tangent);
            return seed;
        }
    };
}

// Key for deduplication by position + UV (Option B)
struct VertexKeyPU
{
    glm::vec3 pos{};
    glm::vec2 uv{};
    bool operator==(const VertexKeyPU &other) const noexcept
    {
        return pos == other.pos && uv == other.uv;
    }
};

namespace std
{
    template <>
    struct hash<VertexKeyPU>
    {
        size_t operator()(const VertexKeyPU &k) const noexcept
        {
            size_t seed = 0u;
            auto hash_combine = [&seed](auto const &val)
            {
                seed ^= std::hash<std::decay_t<decltype(val)>>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };
            hash_combine(k.pos);
            hash_combine(k.uv);
            return seed;
        }
    };
}

// model info
struct Model
{
    struct Mesh
    {
        std::vector<Primitive> primitives;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        VulkanBuffer shaderMaterialBuffer;
        VulkanBuffer shaderMeshBuffer;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE; // using a descriptor for each mesh
    };

    struct Node
    {
        Node *parent = nullptr;
        std::vector<Node *> children;
        int32_t meshIndex = -1; // index into Model::meshes
        glm::mat4 matrix{1.0f};
        std::string name;
        bool visible = true;
        ~Node()
        {
            for (auto *c : children)
                delete c;
        }
    };
    enum class AlphaMode
    {
        Opaque,
        Mask,
        Blend
    };

    struct Material
    {
        // CPU-side description
        glm::vec4 baseColorFactor{1.0f};
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        AlphaMode alphaMode = AlphaMode::Opaque;

        int32_t baseColorTextureIndex = -1;
        int32_t normalTextureIndex = -1;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        uint32_t uboOffset = 0;
    };

    struct Image
    {
        TextureGPU gpu; // raw Vulkan handles
    };

    struct Texture
    {
        int32_t imageIndex = -1;
    };

    std::vector<Node *> nodes;
    std::vector<Mesh> meshes;
    std::vector<vertex> vertices;
    // Option B: deduplicate by position+UV
    std::unordered_map<VertexKeyPU, uint32_t> uniqueVertices{};
    // Accumulators to average normals/tangents over duplicates
    std::vector<glm::vec3> normalSums;
    std::vector<uint32_t> normalCounts;
    std::vector<glm::vec4> tangentSums;
    std::vector<uint32_t> tangentCounts;
    std::vector<uint32_t> indices;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::string name;

    void destroy(VkDevice device);
};

// gltf loader/manager
class vulkanGLTF
{
public:
    VulkanDevice *device = nullptr;
    std::vector<Model> models;

    // load a gltf file and return its index in the models vector
    int loadglTFFile(std::string filename);

    // get a model by index
    Model *getModel(size_t index);

    // get number of loaded models
    size_t getModelCount() const { return models.size(); }

    // destroy all models
    void destroy();

    // destroy a specific model by index
    void destroyModel(size_t index);

private:
    // Load vertices for a primitive and output mapping from the primitive's
    // local vertex index to the model's unique vertex index (deduplicated).
    void loadVertices(const tinygltf::Model &gltfModel, const tinygltf::Primitive &primitive, Model &model, std::vector<uint32_t> &outLocalToUnique);
    // Load indices for a primitive, remapping through the provided local->unique
    // vertex index table (deduplicated indices into model.vertices).
    void loadIndices(const tinygltf::Model &gltfModel, const tinygltf::Primitive &primitive, Model &model, const std::vector<uint32_t> &localToUnique);
    void loadNode(const tinygltf::Model &gltfModel, const tinygltf::Node &inputNode, Model::Node *parent, Model &model);
    void createBuffers(Model &model);
    void loadMaterials(tinygltf::Model &input, Model &model);
    void loadTextures(tinygltf::Model &input, Model &model);
    void createTextureImage(TextureGPU &texture, const void *data, VkDeviceSize size, uint32_t width, uint32_t height, VkFormat format);
};
