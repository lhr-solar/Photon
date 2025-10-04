#include "vulkanGLTF.hpp"
#include "vulkan/vulkan.h"
#include "vulkanDevice.hpp"
#include "vulkanBuffer.hpp"
#include "../engine/include.hpp"
#include <glm/gtc/type_ptr.hpp>  
#include <glm/common.hpp>  
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm> 




#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION
#define JSON_NOEXCEPTION

#include "tiny_gltf.h"
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


bool vulkanGLTF::loadglTFFile(std::string filename)
{
    tinygltf::TinyGLTF gltfLoader;
    tinygltf::Model gltfModel;
    std::string err;
    std::string warn;
    
    bool ret = false;
    if (filename.find(".glb") != std::string::npos) {
        ret = gltfLoader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
    } else {
        ret = gltfLoader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);
    }
    
    if (!warn.empty()) {
        logs("[!] GLTF Warning: " << warn);
    }
    
    if (!err.empty()) {
        logs("[!] GLTF Error: " << err);
    }
    
    if (!ret) {
        logs("[!] Failed to load GLTF model: " << filename);
        return false;
    }
    
    logs("[+] Successfully loaded GLTF model: " << filename);
    
    // Load all meshes
    for (const auto& gltfMesh : gltfModel.meshes) {
        Model::Mesh mesh;
        
        for (const auto& primitive : gltfMesh.primitives) {
            Primitive prim;
            prim.firstIndex = static_cast<uint32_t>(model.indices.size());
            
            loadVertices(gltfModel, primitive);
            
            // Load index data
            loadIndices(gltfModel, primitive);
            
            prim.indexCount = static_cast<uint32_t>(model.indices.size()) - prim.firstIndex;
            prim.materialIndex = primitive.material;
            
            mesh.primitives.push_back(prim);
        }
        
        model.meshes.push_back(mesh);
    }
    
    // Load scene nodes
    if (!gltfModel.scenes.empty()) {
        for (const auto& gltfNode : gltfModel.scenes[0].nodes) {
            if (gltfNode >= 0 && gltfNode < gltfModel.nodes.size()) {
                Model::Node* node = new Model::Node();
                loadNode(gltfModel, gltfModel.nodes[gltfNode], node);
                model.nodes.push_back(node);
            }
        }
    }
    
    // Create Vulkan buffers
    createBuffers();
    
    logs("[+] GLTF model loaded with " << model.vertices.size() << " vertices and " << model.indices.size() << " indices");
    return true;
}

void vulkanGLTF::loadVertices(const tinygltf::Model& gltfModel, const tinygltf::Primitive& primitive)
{
    const float* positionBuffer = nullptr;
    const float* normalsBuffer = nullptr;
    const float* texCoordsBuffer = nullptr;
    const float* colorsBuffer = nullptr;
    const float* tangentsBuffer = nullptr;
    const float* jointsBuffer = nullptr;
    const float* weightsBuffer = nullptr;
    
    size_t vertexCount = 0;
    
    // Get vertex positions
    if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.find("POSITION")->second];
        const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
        positionBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
        vertexCount = accessor.count;
    }
    
    // Get vertex normals
    if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.find("NORMAL")->second];
        const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
        normalsBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
    }
    
    // Get vertex texture coordinates
    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.find("TEXCOORD_0")->second];
        const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
        texCoordsBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
    }
    
    // Get vertex colors
    if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.find("COLOR_0")->second];
        const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
        colorsBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
    }
    
    // Get vertex tangents
    if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.find("TANGENT")->second];
        const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
        tangentsBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
    }

    
    
    // Create vertices
    for (size_t v = 0; v < vertexCount; v++) {
        vertex vert{};
        
        // Position
        if (positionBuffer) {
            vert.pos = glm::vec3(
                positionBuffer[v * 3 + 0],
                positionBuffer[v * 3 + 1],
                positionBuffer[v * 3 + 2]
            );
        }
        
        // Normal
        if (normalsBuffer) {
            vert.normal = glm::vec3(
                normalsBuffer[v * 3 + 0],
                normalsBuffer[v * 3 + 1],
                normalsBuffer[v * 3 + 2]
            );
        }
        
        // Texture coordinates
        if (texCoordsBuffer) {
            vert.uv = glm::vec2(
                texCoordsBuffer[v * 2 + 0],
                texCoordsBuffer[v * 2 + 1]
            );
        }
        
        // Color
        if (colorsBuffer) {
            vert.color = glm::vec4(
                colorsBuffer[v * 4 + 0],
                colorsBuffer[v * 4 + 1],
                colorsBuffer[v * 4 + 2],
                colorsBuffer[v * 4 + 3]
            );
        } else {
            vert.color = glm::vec4(1.0f); 
        }
        
        // Tangent
        if (tangentsBuffer) {
            vert.tangent = glm::vec4(
                tangentsBuffer[v * 4 + 0],
                tangentsBuffer[v * 4 + 1],
                tangentsBuffer[v * 4 + 2],
                tangentsBuffer[v * 4 + 3]
            );
        }
        

        model.vertices.push_back(vert);
    }
}

void vulkanGLTF::loadIndices(const tinygltf::Model& gltfModel, const tinygltf::Primitive& primitive)
{
    if (primitive.indices >= 0) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.indices];
        const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
        
        const void* dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);
        
        switch (accessor.componentType) {
            case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                for (size_t index = 0; index < accessor.count; index++) {
                    model.indices.push_back(buf[index]);
                }
                break;
            }
            case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                for (size_t index = 0; index < accessor.count; index++) {
                    model.indices.push_back(buf[index]);
                }
                break;
            }
            case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                for (size_t index = 0; index < accessor.count; index++) {
                    model.indices.push_back(buf[index]);
                }
                break;
            }
            default:
                logs("[!] Index component type " << accessor.componentType << " not supported!");
                return;
        }
    }
}

void vulkanGLTF::loadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& inputNode, Model::Node* parent)
{
    Model::Node* node = new Model::Node();
    node->name = inputNode.name;
	node->parent = parent;
    
    node->matrix = glm::mat4(1.0f);
	if (inputNode.translation.size() == 3) {
		node->matrix = glm::translate(node->matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
	}
	if (inputNode.rotation.size() == 4) {
		glm::quat q = glm::make_quat(inputNode.rotation.data());
		node->matrix *= glm::mat4(q);
	}
	if (inputNode.scale.size() == 3) {
		node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
	}
	if (inputNode.matrix.size() == 16) {
		node->matrix = glm::make_mat4x4(inputNode.matrix.data());
	};
    
    // Load mesh if present
    if (inputNode.mesh >= 0 && inputNode.mesh < model.meshes.size()) {
        node->mesh = model.meshes[inputNode.mesh];
    }
    
    // Load children
    for (int child : inputNode.children) {
        if (child >= 0 && child < gltfModel.nodes.size()) {
            loadNode(gltfModel, gltfModel.nodes[child], node);
        }
    }
    
    if (parent) {
        parent->children.push_back(node);
    }
}

void vulkanGLTF::createBuffers()
{
    if (model.vertices.empty()) {
        logs("[!] No vertices to create buffer for");
        return;
    }
    
    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(vertex) * model.vertices.size();
    VkResult result = device->createBuffer(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &model.meshes[0].vertexBuffer,
        vertexBufferSize,
        model.vertices.data()
    );
    
    if (result != VK_SUCCESS) {
        logs("[!] Failed to create vertex buffer");
        return;
    }
    
    model.meshes[0].vertexCount = static_cast<uint32_t>(model.vertices.size());
    
    // Create index buffer
    if (!model.indices.empty()) {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * model.indices.size();
        result = device->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &model.meshes[0].indexBuffer,
            indexBufferSize,
            model.indices.data()
        );
        
        if (result != VK_SUCCESS) {
            logs("[!] Failed to create index buffer");
            return;
        }
        
        model.meshes[0].indexCount = static_cast<uint32_t>(model.indices.size());
    }
    
    logs("[+] Created vertex buffer with " << model.meshes[0].vertexCount << " vertices");
    logs("[+] Created index buffer with " << model.meshes[0].indexCount << " indices");
}

void vulkanGLTF::destroy()
{
    for (auto& mesh : model.meshes) {
        mesh.vertexBuffer.destroy();
        mesh.indexBuffer.destroy();
    }
    model.destroy(device->logicalDevice);
}

void Model::destroy(VkDevice device)
{
    for (auto node : nodes) {
        delete node;
    }
    nodes.clear();
    meshes.clear();
    vertices.clear();
    indices.clear();
} 