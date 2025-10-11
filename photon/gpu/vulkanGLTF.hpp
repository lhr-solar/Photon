#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <memory>

#include "vulkanDevice.hpp"
#include "vulkanBuffer.hpp"

#define VK_CHECK(x) do { VkResult err = x; if (err) { \
    std::cout << "Detected Vulkan error: " << err << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
    abort(); \
} } while(0)

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

struct alignas(16) ShaderMeshData {
		glm::mat4 matrix;
		glm::mat4 jointMatrix[128]{};
		uint32_t jointcount{ 0 };
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
    Model* getModel(size_t index);
    
    // get number of loaded models
    size_t getModelCount() const { return models.size(); }
    
    // destroy all models
    void destroy();
    
    // destroy a specific model by index
    void destroyModel(size_t index);

private:
    void loadVertices(const tinygltf::Model &gltfModel, const tinygltf::Primitive &primitive, Model &model);
    void loadIndices(const tinygltf::Model &gltfModel, const tinygltf::Primitive &primitive, Model &model);
    void loadNode(const tinygltf::Model &gltfModel, const tinygltf::Node &inputNode, Model::Node *parent, Model &model);
    void createBuffers(Model &model);
    void loadMaterials(tinygltf::Model &input, Model &model);
    void loadTextures(tinygltf::Model &input, Model &model);
};
