#include "vulkanGLTF.hpp"
#include "vulkan/vulkan.h"
#include "gpu.hpp"
#include "vulkan_core.h"
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// global vertex values
// TODO should this be global?
VkVertexInputBindingDescription Vertex::vertexInputBindingDescription;
std::vector<VkVertexInputAttributeDescription> Vertex::vertexInputAttributeDescriptions;
VkPipelineVertexInputStateCreateInfo Vertex::pipelineVertexInputStateCreateInfo;

VkVertexInputBindingDescription Vertex::inputBindingDescription(uint32_t binding) {
	return VkVertexInputBindingDescription({ binding, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
}

VkVertexInputAttributeDescription Vertex::inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component) {
	switch (component) {
		case VertexComponent::Position: 
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) });
		case VertexComponent::Normal:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
		case VertexComponent::UV:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
		case VertexComponent::Color:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) });
		case VertexComponent::Tangent:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)} );
		case VertexComponent::Joint0:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joint0) });
		case VertexComponent::Weight0:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0) });
		default:
			return VkVertexInputAttributeDescription({});
	}
}

std::vector<VkVertexInputAttributeDescription> Vertex::inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components) {
	std::vector<VkVertexInputAttributeDescription> result;
	uint32_t location = 0;
	for (VertexComponent component : components) {
		result.push_back(Vertex::inputAttributeDescription(binding, location, component));
		location++;
	}
	return result;
}

/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
VkPipelineVertexInputStateCreateInfo* Vertex::getPipelineVertexInputState(const std::vector<VertexComponent> components) {
    vertexInputBindingDescription = Vertex::inputBindingDescription(0);
    Vertex::vertexInputAttributeDescriptions = Vertex::inputAttributeDescriptions(0, components);
	pipelineVertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
	pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = &Vertex::vertexInputBindingDescription;
	pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(Vertex::vertexInputAttributeDescriptions.size());
	pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = Vertex::vertexInputAttributeDescriptions.data();
	return &pipelineVertexInputStateCreateInfo;
}

void Material::createDescriptorSet(VkDevice logicalDevice, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags){
    	VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptorSetLayout,
	};
	VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &descriptorSetAllocInfo, &descriptorSet));
	std::vector<VkDescriptorImageInfo> imageDescriptors{};
	std::vector<VkWriteDescriptorSet> writeDescriptorSets{};
	if (descriptorBindingFlags & 0x01) {
		imageDescriptors.push_back(baseColorTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptorSet,
			.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size()),
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &baseColorTexture->descriptor
		};
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	if (normalTexture && descriptorBindingFlags & 0x02) {
		imageDescriptors.push_back(normalTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptorSet,
			.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size()),
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &normalTexture->descriptor
		};
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

Mesh::Mesh(VulkanDevice vulkanDevice, glm::mat4 matrix){
    VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
            &uniformBuffer, sizeof(uniformBlock), &uniformBlock));
    VK_CHECK(vkMapMemory(vulkanDevice.logicalDevice, uniformBuffer.memory, 0, sizeof(uniformBlock), 0, &uniformBuffer.mapped));
    uniformBuffer.descriptor = {uniformBuffer.buffer, 0, sizeof(uniformBlock)};
}

void Model::draw(VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet){
    if(!bufferBound){
        const VkDeviceSize offsets[1] = {0};
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
    for(auto& node : nodes){
        drawNode(node, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
    }
}

void Model::drawNode(Node *node, VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet){
    if(node->mesh){
        for(Primitive* primitive : node->mesh->primitives){
            bool s = 0;
            const Material& m = primitive->material;
            if(renderFlags & RenderOpaqueNodes){
                s = (m.alphaMode != ALPHAMODE_OPAQUE);
            }
            if(renderFlags & RenderAlphaMaskedNodes){
                s = (m.alphaMode != ALPHAMODE_MASK);
            }
            if(renderFlags & RenderAlphaBlendedNodes){
                s = (m.alphaMode != ALPHAMODE_BLEND);
            }
            if(!s){
                if(renderFlags & BindImages){
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, bindImageSet, 1, &m.descriptorSet, 0, nullptr);
                }
                vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
            }
        }
    }
    for(auto& child : node->children){
        drawNode(child, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
    }
}

bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, 
        int req_width, int req_height, const unsigned char* bytes, int size, void* userData){
    if (image->uri.find_last_of(".") != std::string::npos) {
        if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") { return true; } }
	return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
}
bool loadImageDataFuncEmpty(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, 
        int req_width, int req_height, const unsigned char* bytes, int size, void* userData){ return true; }

void Model::loadFromFile(std::string fileName, VulkanDevice* device, VkQueue transferQueue, uint32_t fileLoadingFlags, float scale){
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF gltfContext;
    if(fileLoadingFlags & FileLoadingFlags::DontLoadImages){
        gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
	}else{
        gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
	}

    size_t pos = fileName.find_last_of('/');
//	path = fileName.substr(0, pos);
	std::string error, warning;
//	this->device = device;

}
