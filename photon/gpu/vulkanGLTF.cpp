#include "vulkanGLTF.hpp"
#include "vulkan/vulkan.h"

// global vertex values
// TODO should this be global?
VkVertexInputBindingDescription vertex::vertexInputBindingDescription;
std::vector<VkVertexInputAttributeDescription> vertex::vertexInputAttributeDescriptions;
VkPipelineVertexInputStateCreateInfo vertex::pipelineVertexInputStateCreateInfo;

VkVertexInputBindingDescription vertex::inputBindingDescription(uint32_t binding) {
	return VkVertexInputBindingDescription({ binding, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX });
}

VkVertexInputAttributeDescription vertex::inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component) {
	switch (component) {
		case VertexComponent::Position: 
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, pos) });
		case VertexComponent::Normal:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, normal) });
		case VertexComponent::UV:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, uv) });
		case VertexComponent::Color:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, color) });
		case VertexComponent::Tangent:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, tangent)} );
		case VertexComponent::Joint0:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, joint0) });
		case VertexComponent::Weight0:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, weight0) });
		default:
			return VkVertexInputAttributeDescription({});
	}
}

std::vector<VkVertexInputAttributeDescription> vertex::inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components) {
	std::vector<VkVertexInputAttributeDescription> result;
	uint32_t location = 0;
	for (VertexComponent component : components) {
		result.push_back(vertex::inputAttributeDescription(binding, location, component));
		location++;
	}
	return result;
}

/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
VkPipelineVertexInputStateCreateInfo* vertex::getPipelineVertexInputState(const std::vector<VertexComponent> components) {
    vertexInputBindingDescription = vertex::inputBindingDescription(0);
    vertex::vertexInputAttributeDescriptions = vertex::inputAttributeDescriptions(0, components);
	pipelineVertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
	pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = &vertex::vertexInputBindingDescription;
	pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex::vertexInputAttributeDescriptions.size());
	pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = vertex::vertexInputAttributeDescriptions.data();
	return &pipelineVertexInputStateCreateInfo;
}

