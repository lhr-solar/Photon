#include "vulkanGLTF.hpp"
#include "../engine/include.hpp"
#include "vulkan/vulkan.h"
#include "vulkanBuffer.hpp"
#include "vulkanDevice.hpp"
#include <algorithm>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION
#define JSON_NOEXCEPTION

#include "tiny_gltf.h"
// global vertex values
// TODO should this be global?
VkVertexInputBindingDescription vertex::vertexInputBindingDescription;
std::vector<VkVertexInputAttributeDescription>
    vertex::vertexInputAttributeDescriptions;
VkPipelineVertexInputStateCreateInfo vertex::pipelineVertexInputStateCreateInfo;

VkVertexInputBindingDescription
vertex::inputBindingDescription(uint32_t binding)
{
    return VkVertexInputBindingDescription(
        {binding, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX});
}

VkVertexInputAttributeDescription
vertex::inputAttributeDescription(uint32_t binding, uint32_t location,
                                  VertexComponent component)
{
    switch (component)
    {
    case VertexComponent::Position:
        return VkVertexInputAttributeDescription(
            {location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, pos)});
    case VertexComponent::Normal:
        return VkVertexInputAttributeDescription({location, binding,
                                                  VK_FORMAT_R32G32B32_SFLOAT,
                                                  offsetof(vertex, normal)});
    case VertexComponent::UV:
        return VkVertexInputAttributeDescription(
            {location, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, uv)});
    case VertexComponent::Color:
        return VkVertexInputAttributeDescription({location, binding,
                                                  VK_FORMAT_R32G32B32A32_SFLOAT,
                                                  offsetof(vertex, color)});
    case VertexComponent::Tangent:
        return VkVertexInputAttributeDescription({location, binding,
                                                  VK_FORMAT_R32G32B32A32_SFLOAT,
                                                  offsetof(vertex, tangent)});
    case VertexComponent::Joint0:
        return VkVertexInputAttributeDescription({location, binding,
                                                  VK_FORMAT_R32G32B32A32_SFLOAT,
                                                  offsetof(vertex, joint0)});
    case VertexComponent::Weight0:
        return VkVertexInputAttributeDescription({location, binding,
                                                  VK_FORMAT_R32G32B32A32_SFLOAT,
                                                  offsetof(vertex, weight0)});
    default:
        return VkVertexInputAttributeDescription({});
    }
}

std::vector<VkVertexInputAttributeDescription>
vertex::inputAttributeDescriptions(
    uint32_t binding, const std::vector<VertexComponent> components)
{
    std::vector<VkVertexInputAttributeDescription> result;
    uint32_t location = 0;
    for (VertexComponent component : components)
    {
        result.push_back(
            vertex::inputAttributeDescription(binding, location, component));
        location++;
    }
    return result;
}

/** @brief Returns the default pipeline vertex input state create info structure
 * for the requested vertex components */
VkPipelineVertexInputStateCreateInfo *vertex::getPipelineVertexInputState(
    const std::vector<VertexComponent> components)
{
    vertexInputBindingDescription = vertex::inputBindingDescription(0);
    vertex::vertexInputAttributeDescriptions =
        vertex::inputAttributeDescriptions(0, components);
    pipelineVertexInputStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
    pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions =
        &vertex::vertexInputBindingDescription;
    pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(vertex::vertexInputAttributeDescriptions.size());
    pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions =
        vertex::vertexInputAttributeDescriptions.data();
    return &pipelineVertexInputStateCreateInfo;
}

int vulkanGLTF::loadglTFFile(std::string filename)
{
    tinygltf::TinyGLTF gltfLoader;
    tinygltf::Model gltfModel;
    std::string err;
    std::string warn;

    bool ret = false;
    if (filename.find(".glb") != std::string::npos)
    {
        ret = gltfLoader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
    }
    else
    {
        ret = gltfLoader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);
    }

    if (!warn.empty())
    {
        logs("[!] GLTF Warning: " << warn);
    }

    if (!err.empty())
    {
        logs("[!] GLTF Error: " << err);
    }

    if (!ret)
    {
        logs("[!] Failed to load GLTF model: " << filename);
        return -1;
    }

    logs("[+] Successfully loaded GLTF model: " << filename);

    models.emplace_back();
    Model &model = models.back();
    model.name = filename;
    int modelIndex = static_cast<int>(models.size() - 1);

    // Load materials and textures first (they're referenced by meshes)
    loadMaterials(gltfModel, model);
    loadTextures(gltfModel, model);

    // Load all meshes
    for (const auto &gltfMesh : gltfModel.meshes)
    {
        Model::Mesh mesh;

        for (const auto &primitive : gltfMesh.primitives)
        {
            Primitive prim;
            prim.firstIndex = static_cast<uint32_t>(model.indices.size());

            loadVertices(gltfModel, primitive, model);

            // Load index data
            loadIndices(gltfModel, primitive, model);

            prim.indexCount =
                static_cast<uint32_t>(model.indices.size()) - prim.firstIndex;
            prim.materialIndex = primitive.material;

            mesh.primitives.push_back(prim);
        }

        model.meshes.push_back(mesh);
    }

    // Load scene nodes
    if (!gltfModel.scenes.empty())
    {
        for (const auto &gltfNode : gltfModel.scenes[0].nodes)
        {
            if (gltfNode >= 0 && gltfNode < gltfModel.nodes.size())
            {
                Model::Node *node = new Model::Node();
                loadNode(gltfModel, gltfModel.nodes[gltfNode], node, model);
                model.nodes.push_back(node);
            }
        }
    }

    // Create Vulkan buffers
    createBuffers(model);

    logs("[+] GLTF model loaded with " << model.vertices.size()
                                       << " vertices and " << model.indices.size()
                                       << " indices");
    logs("[+] GLTF model loaded with " << model.materials.size() << " materials");

    // Calculate model bounds for debugging
    if (!model.vertices.empty())
    {
        logs("[DEBUG] Calculating model bounds for " << model.vertices.size() << " vertices");
        glm::vec3 minBounds = model.vertices[0].pos;
        glm::vec3 maxBounds = model.vertices[0].pos;

        for (const auto &vertex : model.vertices)
        {
            minBounds.x = std::min<float>(minBounds.x, vertex.pos.x);
            minBounds.y = std::min<float>(minBounds.y, vertex.pos.y);
            minBounds.z = std::min<float>(minBounds.z, vertex.pos.z);
            maxBounds.x = std::max<float>(maxBounds.x, vertex.pos.x);
            maxBounds.y = std::max<float>(maxBounds.y, vertex.pos.y);
            maxBounds.z = std::max<float>(maxBounds.z, vertex.pos.z);
        }

        glm::vec3 center = (minBounds + maxBounds) * 0.5f;
        glm::vec3 size = maxBounds - minBounds;

        logs("[DEBUG] Model bounds - Min: (" << minBounds.x << ", " << minBounds.y << ", " << minBounds.z << ")");
        logs("[DEBUG] Model bounds - Max: (" << maxBounds.x << ", " << maxBounds.y << ", " << maxBounds.z << ")");
        logs("[DEBUG] Model center: (" << center.x << ", " << center.y << ", " << center.z << ")");
        logs("[DEBUG] Model size: (" << size.x << ", " << size.y << ", " << size.z << ")");
    }
    else
    {
        logs("[DEBUG] No vertices found in model for bounds calculation");
    }

    return modelIndex;
}

void vulkanGLTF::loadVertices(const tinygltf::Model &gltfModel,
                              const tinygltf::Primitive &primitive,
                              Model &model)
{
    const float *positionBuffer = nullptr;
    const float *normalsBuffer = nullptr;
    const float *texCoordsBuffer = nullptr;
    const float *colorsBuffer = nullptr;
    const float *tangentsBuffer = nullptr;
    const float *jointsBuffer = nullptr;
    const float *weightsBuffer = nullptr;

    size_t vertexCount = 0;

    // Get vertex positions
    if (primitive.attributes.find("POSITION") != primitive.attributes.end())
    {
        const tinygltf::Accessor &accessor =
            gltfModel.accessors[primitive.attributes.find("POSITION")->second];
        const tinygltf::BufferView &view =
            gltfModel.bufferViews[accessor.bufferView];
        positionBuffer = reinterpret_cast<const float *>(
            &(gltfModel.buffers[view.buffer]
                  .data[accessor.byteOffset + view.byteOffset]));
        vertexCount = accessor.count;
    }

    // Get vertex normals
    if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
    {
        const tinygltf::Accessor &accessor =
            gltfModel.accessors[primitive.attributes.find("NORMAL")->second];
        const tinygltf::BufferView &view =
            gltfModel.bufferViews[accessor.bufferView];
        normalsBuffer = reinterpret_cast<const float *>(
            &(gltfModel.buffers[view.buffer]
                  .data[accessor.byteOffset + view.byteOffset]));
    }

    // Get vertex texture coordinates
    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
    {
        const tinygltf::Accessor &accessor =
            gltfModel.accessors[primitive.attributes.find("TEXCOORD_0")->second];
        const tinygltf::BufferView &view =
            gltfModel.bufferViews[accessor.bufferView];
        texCoordsBuffer = reinterpret_cast<const float *>(
            &(gltfModel.buffers[view.buffer]
                  .data[accessor.byteOffset + view.byteOffset]));
    }

    // Get vertex colors
    if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
    {
        const tinygltf::Accessor &accessor =
            gltfModel.accessors[primitive.attributes.find("COLOR_0")->second];
        const tinygltf::BufferView &view =
            gltfModel.bufferViews[accessor.bufferView];
        colorsBuffer = reinterpret_cast<const float *>(
            &(gltfModel.buffers[view.buffer]
                  .data[accessor.byteOffset + view.byteOffset]));
    }

    // Get vertex tangents
    if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
    {
        const tinygltf::Accessor &accessor =
            gltfModel.accessors[primitive.attributes.find("TANGENT")->second];
        const tinygltf::BufferView &view =
            gltfModel.bufferViews[accessor.bufferView];
        tangentsBuffer = reinterpret_cast<const float *>(
            &(gltfModel.buffers[view.buffer]
                  .data[accessor.byteOffset + view.byteOffset]));
    }

    // Create vertices
    for (size_t v = 0; v < vertexCount; v++)
    {
        vertex vert{};

        // Position
        if (positionBuffer)
        {
            vert.pos = glm::vec3(positionBuffer[v * 3 + 0], positionBuffer[v * 3 + 1],
                                 positionBuffer[v * 3 + 2]);
        }

        // Normal
        if (normalsBuffer)
        {
            vert.normal =
                glm::vec3(normalsBuffer[v * 3 + 0], normalsBuffer[v * 3 + 1],
                          normalsBuffer[v * 3 + 2]);
        }

        // Texture coordinates
        if (texCoordsBuffer)
        {
            vert.uv =
                glm::vec2(texCoordsBuffer[v * 2 + 0], texCoordsBuffer[v * 2 + 1]);
        }

        // Color - CRITICAL: default to white with full opacity if no vertex colors
        if (colorsBuffer)
        {
            vert.color = glm::vec4(colorsBuffer[v * 4 + 0], colorsBuffer[v * 4 + 1],
                                   colorsBuffer[v * 4 + 2], colorsBuffer[v * 4 + 3]);
            // Ensure alpha is valid
            if (vert.color.a < 0.01f) {
                vert.color.a = 1.0f;
            }
        }
        else
        {
            // No vertex colors - use white with full opacity (RGBA = 1,1,1,1)
            vert.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        // Tangent
        if (tangentsBuffer)
        {
            vert.tangent =
                glm::vec4(tangentsBuffer[v * 4 + 0], tangentsBuffer[v * 4 + 1],
                          tangentsBuffer[v * 4 + 2], tangentsBuffer[v * 4 + 3]);
        }

        model.vertices.push_back(vert);
    }
}

void vulkanGLTF::loadIndices(const tinygltf::Model &gltfModel,
                             const tinygltf::Primitive &primitive,
                             Model &model)
{
    if (primitive.indices >= 0)
    {
        const tinygltf::Accessor &accessor = gltfModel.accessors[primitive.indices];
        const tinygltf::BufferView &bufferView =
            gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

        const void *dataPtr =
            &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

        switch (accessor.componentType)
        {
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
        {
            const uint32_t *buf = static_cast<const uint32_t *>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++)
            {
                model.indices.push_back(buf[index]);
            }
            break;
        }
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
        {
            const uint16_t *buf = static_cast<const uint16_t *>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++)
            {
                model.indices.push_back(buf[index]);
            }
            break;
        }
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
        {
            const uint8_t *buf = static_cast<const uint8_t *>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++)
            {
                model.indices.push_back(buf[index]);
            }
            break;
        }
        default:
            logs("[!] Index component type " << accessor.componentType
                                             << " not supported!");
            return;
        }
    }
}

void vulkanGLTF::loadNode(const tinygltf::Model &gltfModel,
                          const tinygltf::Node &inputNode, Model::Node *parent,
                          Model &model)
{
    Model::Node *node = new Model::Node();
    node->name = inputNode.name;
    node->parent = parent;

    node->matrix = glm::mat4(1.0f);
    if (inputNode.translation.size() == 3)
    {
        node->matrix = glm::translate(
            node->matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
    }
    if (inputNode.rotation.size() == 4)
    {
        glm::quat q = glm::make_quat(inputNode.rotation.data());
        node->matrix *= glm::mat4(q);
    }
    if (inputNode.scale.size() == 3)
    {
        node->matrix = glm::scale(
            node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
    }
    if (inputNode.matrix.size() == 16)
    {
        node->matrix = glm::make_mat4x4(inputNode.matrix.data());
    };

    // Load mesh if present
    if (inputNode.mesh >= 0 && inputNode.mesh < model.meshes.size())
    {
        node->meshIndex = inputNode.mesh;
    }

    // Load children
    for (int child : inputNode.children)
    {
        if (child >= 0 && child < gltfModel.nodes.size())
        {
            loadNode(gltfModel, gltfModel.nodes[child], node, model);
        }
    }

    if (parent)
    {
        parent->children.push_back(node);
    }
}

void vulkanGLTF::createBuffers(Model &model)
{
    if (model.vertices.empty())
    {
        logs("[!] No vertices to create buffer for");
        return;
    }

    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(vertex) * model.vertices.size();
    VkResult result = device->createBuffer(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &model.meshes[0].vertexBuffer, vertexBufferSize, model.vertices.data());

    if (result != VK_SUCCESS)
    {
        logs("[!] Failed to create vertex buffer");
        return;
    }

    model.meshes[0].vertexCount = static_cast<uint32_t>(model.vertices.size());

    // Create index buffer
    if (!model.indices.empty())
    {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * model.indices.size();
        result = device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      &model.meshes[0].indexBuffer, indexBufferSize,
                                      model.indices.data());

        if (result != VK_SUCCESS)
        {
            logs("[!] Failed to create index buffer");
            return;
        }

        model.meshes[0].indexCount = static_cast<uint32_t>(model.indices.size());
    }

    logs("[+] Created vertex buffer with " << model.meshes[0].vertexCount
                                           << " vertices");
    logs("[+] Created index buffer with " << model.meshes[0].indexCount
                                          << " indices");

    for (auto &mesh : model.meshes)
    {
        // create material and texture buffer
        {
            VkDeviceSize bufferSize =
                model.materials.size() * sizeof(Model::Material);
            VulkanBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;
            result = device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          &stagingBuffer, bufferSize,
                                          model.materials.data());
            if (result != VK_SUCCESS)
            {
                logs("[!] Failed to create texture staging buffer");
                return;
            }
            result = device->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mesh.shaderMaterialBuffer,
                bufferSize, nullptr);
            if (result != VK_SUCCESS)
            {
                logs("[!] Failed to create shader material buffer");
                return;
            }
            logs("[+] mat and text buffers complete");
            // Copy from staging buffers
            VkCommandBuffer copyCmd = device->createCommandBuffer(
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, device->transferCommandPool, true);
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer,
                            mesh.shaderMaterialBuffer.buffer, 1, &copyRegion);
            device->flushCommandBuffer(copyCmd, device->transferQueue,
                                       device->transferCommandPool, true);
            stagingBuffer.destroy();

            mesh.shaderMaterialBuffer.descriptor.buffer =
                mesh.shaderMaterialBuffer.buffer;
            mesh.shaderMaterialBuffer.descriptor.offset = 0;
            mesh.shaderMaterialBuffer.descriptor.range = bufferSize;
        }
        logs("[+] staged command buffers");
        { // future skeleton support
            ShaderMeshData meshData{};
            meshData.matrix = glm::mat4(1.0f);
            meshData.jointcount = 0;

            VkDeviceSize bufferSize = sizeof(ShaderMeshData);
            VulkanBuffer stagingBuffer;
            VK_CHECK(device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          &stagingBuffer, bufferSize, &meshData));
            VK_CHECK(device->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mesh.shaderMeshBuffer,
                bufferSize, nullptr));

            // Copy from staging buffers
            VkCommandBuffer copyCmd = device->createCommandBuffer(
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, device->transferCommandPool, true);
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer,
                            mesh.shaderMeshBuffer.buffer, 1, &copyRegion);
            device->flushCommandBuffer(copyCmd, device->transferQueue,
                                       device->transferCommandPool, true);
            stagingBuffer.destroy();

            mesh.shaderMeshBuffer.descriptor.buffer = mesh.shaderMeshBuffer.buffer;
            mesh.shaderMeshBuffer.descriptor.offset = 0;
            mesh.shaderMeshBuffer.descriptor.range = bufferSize;
        }
    }
    logs("[+] gtlf buffers complete");
}

void vulkanGLTF::loadMaterials(tinygltf::Model &input, Model &model)
{
    model.materials.resize(input.materials.size());
    for (size_t i = 0; i < input.materials.size(); i++)
    {
        // We only read the most basic properties required for our sample
        tinygltf::Material glTFMaterial = input.materials[i];
        // Get the base color factor
        if (glTFMaterial.values.find("baseColorFactor") !=
            glTFMaterial.values.end())
        {
            model.materials[i].baseColorFactor = glm::make_vec4(
                glTFMaterial.values["baseColorFactor"].ColorFactor().data());
        }
        // Get base color texture index
        if (glTFMaterial.values.find("baseColorTexture") !=
            glTFMaterial.values.end())
        {
            model.materials[i].baseColorTextureIndex =
                glTFMaterial.values["baseColorTexture"].TextureIndex();
        }
        // Get the normal map texture index
        if (glTFMaterial.additionalValues.find("normalTexture") !=
            glTFMaterial.additionalValues.end())
        {
            model.materials[i].normalTextureIndex =
                glTFMaterial.additionalValues["normalTexture"].TextureIndex();
        }
        // Get some additional material parameters that are used in this sample
        // Convert alpha mode string to enum
        if (glTFMaterial.alphaMode == "BLEND")
        {
            model.materials[i].alphaMode = Model::AlphaMode::Blend;
        }
        else if (glTFMaterial.alphaMode == "MASK")
        {
            model.materials[i].alphaMode = Model::AlphaMode::Mask;
        }
        else
        {
            model.materials[i].alphaMode = Model::AlphaMode::Opaque;
        }
        model.materials[i].alphaCutoff =
            static_cast<float>(glTFMaterial.alphaCutoff);
        model.materials[i].doubleSided = glTFMaterial.doubleSided;
    }
}

void vulkanGLTF::loadTextures(tinygltf::Model &input, Model &model)
{
    model.textures.resize(input.textures.size());
    for (size_t i = 0; i < input.textures.size(); i++)
    {
        model.textures[i].imageIndex = input.textures[i].source;
    }

    // Load images
    model.images.resize(input.images.size());
    for (size_t i = 0; i < input.images.size(); i++)
    {
        tinygltf::Image &gltfImage = input.images[i];

        // Check if image data is available
        if (gltfImage.image.empty())
        {
            logs("[!] Image " << i << " has no data, skipping");
            continue;
        }

        // Create Vulkan image
        TextureGPU &texture = model.images[i].gpu;
        texture.width = gltfImage.width;
        texture.height = gltfImage.height;

        // Determine format
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        size_t imageSize = gltfImage.width * gltfImage.height * 4;

        if (gltfImage.component == 3)
        {
            // RGB to RGBA conversion needed
            std::vector<unsigned char> rgbaData(imageSize);
            for (size_t p = 0; p < gltfImage.width * gltfImage.height; ++p)
            {
                rgbaData[p * 4 + 0] = gltfImage.image[p * 3 + 0];
                rgbaData[p * 4 + 1] = gltfImage.image[p * 3 + 1];
                rgbaData[p * 4 + 2] = gltfImage.image[p * 3 + 2];
                rgbaData[p * 4 + 3] = 255; // Full opacity
            }
            createTextureImage(texture, rgbaData.data(), imageSize, gltfImage.width, gltfImage.height, format);
        }
        else if (gltfImage.component == 4)
        {
            // Already RGBA
            createTextureImage(texture, gltfImage.image.data(), imageSize, gltfImage.width, gltfImage.height, format);
        }
        else
        {
            // Unsupported format - create white texture
            logs("[!] Unsupported image component count: " << gltfImage.component);
            std::vector<unsigned char> whiteData(imageSize, 255);
            createTextureImage(texture, whiteData.data(), imageSize, gltfImage.width, gltfImage.height, format);
        }

        logs("[+] Loaded texture image " << i << " (" << texture.width << "x" << texture.height << ")");
    }
}

Model *vulkanGLTF::getModel(size_t index)
{
    if (index < models.size())
    {
        return &models[index];
    }
    logs("[!] Invalid model index: " << index);
    return nullptr;
}

void vulkanGLTF::destroyModel(size_t index)
{
    if (index >= models.size())
    {
        logs("[!] Invalid model index: " << index);
        return;
    }

    Model &model = models[index];
    for (auto &mesh : model.meshes)
    {
        mesh.vertexBuffer.destroy();
        mesh.indexBuffer.destroy();
    }
    model.destroy(device->logicalDevice);
}

void vulkanGLTF::destroy()
{
    for (size_t i = 0; i < models.size(); ++i)
    {
        Model &model = models[i];
        for (auto &mesh : model.meshes)
        {
            mesh.vertexBuffer.destroy();
            mesh.indexBuffer.destroy();
        }
        model.destroy(device->logicalDevice);
    }
    models.clear();
}

void Model::destroy(VkDevice device)
{
    for (auto node : nodes)
    {
        delete node;
    }

    // Destroy texture resources
    for (auto &image : images)
    {
        if (image.gpu.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, image.gpu.sampler, nullptr);
            image.gpu.sampler = VK_NULL_HANDLE;
        }
        if (image.gpu.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, image.gpu.view, nullptr);
            image.gpu.view = VK_NULL_HANDLE;
        }
        if (image.gpu.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, image.gpu.image, nullptr);
            image.gpu.image = VK_NULL_HANDLE;
        }
        if (image.gpu.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, image.gpu.memory, nullptr);
            image.gpu.memory = VK_NULL_HANDLE;
        }
    }

    nodes.clear();
    meshes.clear();
    vertices.clear();
    indices.clear();
    images.clear();
    textures.clear();
    materials.clear();
}

void vulkanGLTF::createTextureImage(TextureGPU &texture, const void *data, VkDeviceSize size, uint32_t width, uint32_t height, VkFormat format)
{
    texture.width = width;
    texture.height = height;
    texture.format = format;

    logs("[DEBUG] Creating texture: " << width << "x" << height << ", size: " << size << " bytes");

    // Create staging buffer
    VulkanBuffer stagingBuffer;
    VK_CHECK(device->createBuffer(
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        size,
        (void *)data));

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(device->logicalDevice, &imageInfo, nullptr, &texture.image));

    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->logicalDevice, texture.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);

    VK_CHECK(vkAllocateMemory(device->logicalDevice, &allocInfo, nullptr, &texture.memory));
    VK_CHECK(vkBindImageMemory(device->logicalDevice, texture.image, texture.memory, 0));

    // Copy data
    VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, device->transferCommandPool, true);

    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image with proper settings
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;   // Tightly packed
    region.bufferImageHeight = 0; // Tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    device->flushCommandBuffer(copyCmd, device->transferQueue, device->transferCommandPool, true);

    stagingBuffer.destroy();
    
    logs("[DEBUG] Texture image created successfully");

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &texture.view));

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // No mipmaps
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f; // Only one mip level
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VK_CHECK(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &texture.sampler));
    
    logs("[DEBUG] Texture sampler created successfully");
}