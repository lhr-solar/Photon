#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <memory>

#include "vulkanDevice.hpp"
#include "vulkanBuffer.hpp"

// ---- Forward declarations for tinygltf (header stays lightweight)
namespace tinygltf {
    class Model;
    struct Primitive;
    struct Node;
}

// ---- Mesh primitive info
struct Primitive {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t  materialIndex = -1;
};

// ---- Vertex layout
enum class VertexComponent {
    Position,
    Normal,
    UV,
    Color,
    Tangent,
    Joint0,
    Weight0
};

struct vertex {
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
    static VkPipelineVertexInputStateCreateInfo* getPipelineVertexInputState(
        const std::vector<VertexComponent> components);

    // NOTE: removed the bogus static loadglTFFile() here — that belongs to vulkanGLTF.
};

// ---- CPU-side GLTF model aggregation
struct Model {
    struct Mesh {
        std::vector<Primitive> primitives;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        uint32_t vertexCount = 0;
        uint32_t indexCount  = 0;
    };

    struct Node {
        Node* parent = nullptr;
        std::vector<Node*> children;
        Mesh        mesh;     // (optional: could be an index/pointer to meshes[])
        glm::mat4   matrix{1.0f};
        std::string name;
        bool        visible = true;

        ~Node() {
            for (auto* child : children) delete child;
        }
    };

    std::vector<Node*>  nodes;
    std::vector<Mesh>   meshes;
    std::vector<vertex> vertices;
    std::vector<uint32_t> indices;

    void destroy(VkDevice device);
};

// ---- GLTF loader/manager
class vulkanGLTF {
public:
    VulkanDevice* device = nullptr;
    Model model;

    bool loadglTFFile(std::string filename);
    void destroy();

private:
    void loadVertices(const tinygltf::Model& gltfModel, const tinygltf::Primitive& primitive);
    void loadIndices (const tinygltf::Model& gltfModel, const tinygltf::Primitive& primitive);
    void loadNode    (const tinygltf::Model& gltfModel, const tinygltf::Node& inputNode, Model::Node* parent);
    void createBuffers();
};
