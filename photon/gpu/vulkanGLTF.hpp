#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
#include <vector>

enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };
struct vertex{
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
