#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION

#include "vulkanGLTF.hpp"
#include "../engine/include.hpp"
#include "vulkan/vulkan.h"
#include "tiny_gltf.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cctype>
#include <filesystem>

namespace {

glm::mat4 composeNodeTransform(const tinygltf::Node &node)
{
    glm::mat4 matrix(1.0f);

    if (node.matrix.size() == 16)
    {
        matrix = glm::make_mat4(node.matrix.data());
        return matrix;
    }

    if (node.translation.size() == 3)
    {
        matrix = glm::translate(matrix, glm::vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])));
    }

    if (node.rotation.size() == 4)
    {
        glm::quat q(
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]));
        matrix *= glm::mat4_cast(q);
    }

    if (node.scale.size() == 3)
    {
        matrix = glm::scale(matrix, glm::vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])));
    }

    return matrix;
}

inline std::string getFileExtension(const std::string &path)
{
    auto dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos)
        return {};
    std::string ext = path.substr(dotPos + 1);
    for (char &c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

} // namespace

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

void Model::destroy(VkDevice device)
{
    (void)device;
    for (auto &mesh : meshes)
    {
        mesh.vertexBuffer.destroy();
        mesh.indexBuffer.destroy();
        mesh.shaderMaterialBuffer.destroy();
        mesh.shaderMeshBuffer.destroy();
    }

    for (auto *node : nodes)
    {
        delete node;
    }
    nodes.clear();
    meshes.clear();
    vertices.clear();
    indices.clear();
    materials.clear();
    images.clear();
    textures.clear();
    uniqueVertices.clear();
    normalSums.clear();
    normalCounts.clear();
    tangentSums.clear();
    tangentCounts.clear();
}

Model *vulkanGLTF::getModel(size_t index)
{
    if (index >= models.size())
    {
        return nullptr;
    }
    return &models[index];
}

void vulkanGLTF::destroy()
{
    for (size_t i = 0; i < models.size(); ++i)
    {
        models[i].destroy(device ? device->logicalDevice : VK_NULL_HANDLE);
    }
    models.clear();
}

void vulkanGLTF::destroyModel(size_t index)
{
    if (index >= models.size())
    {
        return;
    }
    models[index].destroy(device ? device->logicalDevice : VK_NULL_HANDLE);
    models.erase(models.begin() + static_cast<std::ptrdiff_t>(index));
}

static void readFloatVec(const tinygltf::Model &model,
                         const tinygltf::Accessor &accessor,
                         size_t elemIndex,
                         float *outValues,
                         size_t numComponents,
                         float defaultValue = 0.0f)
{
    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

    size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    size_t accessorStride = accessor.ByteStride(bufferView);
    if (accessorStride == 0)
    {
        accessorStride = componentSize * numComponents;
    }

    const size_t offset = bufferView.byteOffset + accessor.byteOffset + elemIndex * accessorStride;
    const unsigned char *dataPtr = buffer.data.data() + offset;

    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        const float *src = reinterpret_cast<const float *>(dataPtr);
        for (size_t i = 0; i < numComponents; ++i)
        {
            outValues[i] = src[i];
        }
    }
    else
    {
        // Handle normalized integer attributes by converting to float [0,1]
        for (size_t i = 0; i < numComponents; ++i)
        {
            outValues[i] = defaultValue;
        }

        switch (accessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            const auto *src = reinterpret_cast<const uint8_t *>(dataPtr);
            for (size_t i = 0; i < numComponents; ++i)
            {
                outValues[i] = accessor.normalized ? static_cast<float>(src[i]) / 255.0f : static_cast<float>(src[i]);
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        {
            const auto *src = reinterpret_cast<const int8_t *>(dataPtr);
            for (size_t i = 0; i < numComponents; ++i)
            {
                outValues[i] = accessor.normalized ? static_cast<float>(src[i]) / 127.0f : static_cast<float>(src[i]);
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            const auto *src = reinterpret_cast<const uint16_t *>(dataPtr);
            for (size_t i = 0; i < numComponents; ++i)
            {
                outValues[i] = accessor.normalized ? static_cast<float>(src[i]) / 65535.0f : static_cast<float>(src[i]);
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        {
            const auto *src = reinterpret_cast<const int16_t *>(dataPtr);
            for (size_t i = 0; i < numComponents; ++i)
            {
                outValues[i] = accessor.normalized ? static_cast<float>(src[i]) / 32767.0f : static_cast<float>(src[i]);
            }
            break;
        }
        default:
            break;
        }
    }

    for (size_t i = accessor.type == TINYGLTF_TYPE_VEC3 ? 3 : numComponents; i < numComponents; ++i)
    {
        outValues[i] = defaultValue;
    }
}

void vulkanGLTF::loadVertices(const tinygltf::Model &gltfModel,
                              const tinygltf::Primitive &primitive,
                              Model &model,
                              std::vector<uint32_t> &outLocalToUnique)
{
    const auto attrPos = primitive.attributes.find("POSITION");
    if (attrPos == primitive.attributes.end())
    {
        logs("[!] GLTF primitive missing POSITION attribute");
        outLocalToUnique.clear();
        return;
    }

    const tinygltf::Accessor &posAccessor = gltfModel.accessors[attrPos->second];
    const size_t vertexCount = posAccessor.count;

    outLocalToUnique.resize(vertexCount);

    bool hasNormal = primitive.attributes.find("NORMAL") != primitive.attributes.end();
    bool hasUV = primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end();
    bool hasColor = primitive.attributes.find("COLOR_0") != primitive.attributes.end();

    const tinygltf::Accessor *normalAccessor = hasNormal ? &gltfModel.accessors.at(primitive.attributes.at("NORMAL")) : nullptr;
    const tinygltf::Accessor *uvAccessor = hasUV ? &gltfModel.accessors.at(primitive.attributes.at("TEXCOORD_0")) : nullptr;
    const tinygltf::Accessor *colorAccessor = hasColor ? &gltfModel.accessors.at(primitive.attributes.at("COLOR_0")) : nullptr;

    for (size_t v = 0; v < vertexCount; ++v)
    {
        vertex vert{};

        float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        readFloatVec(gltfModel, posAccessor, v, pos, 3, 0.0f);
        vert.pos = glm::vec3(pos[0], pos[1], pos[2]);

        if (normalAccessor)
        {
            float n[4] = {0.0f, 1.0f, 0.0f, 0.0f};
            readFloatVec(gltfModel, *normalAccessor, v, n, 3, 0.0f);
            vert.normal = glm::vec3(n[0], n[1], n[2]);
        }
        else
        {
            vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        if (uvAccessor)
        {
            float uv[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            readFloatVec(gltfModel, *uvAccessor, v, uv, 2, 0.0f);
            vert.uv = glm::vec2(uv[0], uv[1]);
        }
        else
        {
            vert.uv = glm::vec2(0.0f);
        }

        if (colorAccessor)
        {
            float col[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            size_t components = tinygltf::GetNumComponentsInType(colorAccessor->type);
            readFloatVec(gltfModel, *colorAccessor, v, col, components, 1.0f);
            vert.color = glm::vec4(col[0], col[1], col[2], components > 3 ? col[3] : 1.0f);
        }
        else
        {
            vert.color = glm::vec4(1.0f);
        }

        vert.tangent = glm::vec4(0.0f);
        vert.joint0 = glm::vec4(0.0f);
        vert.weight0 = glm::vec4(0.0f);

        uint32_t newIndex = static_cast<uint32_t>(model.vertices.size());
        model.vertices.push_back(vert);
        outLocalToUnique[v] = newIndex;
    }
}

void vulkanGLTF::loadIndices(const tinygltf::Model &gltfModel,
                              const tinygltf::Primitive &primitive,
                              Model &model,
                              const std::vector<uint32_t> &localToUnique)
{
    if (primitive.indices < 0)
    {
        for (uint32_t idx = 0; idx < static_cast<uint32_t>(localToUnique.size()); ++idx)
        {
            model.indices.push_back(localToUnique[idx]);
        }
        return;
    }

    const tinygltf::Accessor &indexAccessor = gltfModel.accessors[primitive.indices];
    const tinygltf::BufferView &bufferView = gltfModel.bufferViews[indexAccessor.bufferView];
    const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

    size_t stride = indexAccessor.ByteStride(bufferView);
    if (stride == 0)
    {
        stride = tinygltf::GetComponentSizeInBytes(indexAccessor.componentType);
    }

    const unsigned char *dataPtr = buffer.data.data() + bufferView.byteOffset + indexAccessor.byteOffset;

    for (size_t i = 0; i < indexAccessor.count; ++i)
    {
        uint32_t indexValue = 0;
        switch (indexAccessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            indexValue = *reinterpret_cast<const uint32_t *>(dataPtr + stride * i);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            indexValue = static_cast<uint32_t>(*reinterpret_cast<const uint16_t *>(dataPtr + stride * i));
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            indexValue = static_cast<uint32_t>(*(dataPtr + stride * i));
            break;
        default:
            break;
        }

        if (indexValue < localToUnique.size())
        {
            model.indices.push_back(localToUnique[indexValue]);
        }
        else
        {
            model.indices.push_back(0);
        }
    }
}

void vulkanGLTF::loadNode(const tinygltf::Model &gltfModel,
                           const tinygltf::Node &inputNode,
                           Model::Node *parent,
                           Model &model)
{
    auto *node = new Model::Node();
    node->parent = parent;
    node->name = inputNode.name;
    node->matrix = composeNodeTransform(inputNode);

    if (parent)
    {
        parent->children.push_back(node);
    }

    if (inputNode.mesh >= 0)
    {
        // All primitives combined into mesh index 0 for now
        node->meshIndex = 0;
    }

    model.nodes.push_back(node);

    for (int childIndex : inputNode.children)
    {
        if (childIndex >= 0 && static_cast<size_t>(childIndex) < gltfModel.nodes.size())
        {
            loadNode(gltfModel, gltfModel.nodes[childIndex], node, model);
        }
    }
}

void vulkanGLTF::createBuffers(Model &model)
{
    if (!device || model.meshes.empty())
    {
        return;
    }

    auto &mesh = model.meshes[0];

    if (!model.vertices.empty())
    {
        VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(model.vertices.size() * sizeof(vertex));
        VK_CHECK(device->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &mesh.vertexBuffer,
            vertexBufferSize,
            model.vertices.data()));
        mesh.vertexCount = static_cast<uint32_t>(model.vertices.size());
    }

    if (!model.indices.empty())
    {
        VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(model.indices.size() * sizeof(uint32_t));
        VK_CHECK(device->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &mesh.indexBuffer,
            indexBufferSize,
            model.indices.data()));
        mesh.indexCount = static_cast<uint32_t>(model.indices.size());
    }
}

void vulkanGLTF::loadMaterials(tinygltf::Model &input, Model &model)
{
    model.materials.clear();
    model.materials.reserve(input.materials.size());

    for (const auto &srcMaterial : input.materials)
    {
        Model::Material material;
        if (srcMaterial.pbrMetallicRoughness.baseColorFactor.size() == 4)
        {
            material.baseColorFactor = glm::vec4(
                static_cast<float>(srcMaterial.pbrMetallicRoughness.baseColorFactor[0]),
                static_cast<float>(srcMaterial.pbrMetallicRoughness.baseColorFactor[1]),
                static_cast<float>(srcMaterial.pbrMetallicRoughness.baseColorFactor[2]),
                static_cast<float>(srcMaterial.pbrMetallicRoughness.baseColorFactor[3]));
        }

        material.doubleSided = srcMaterial.doubleSided;
        material.alphaCutoff = static_cast<float>(srcMaterial.alphaCutoff);

        if (srcMaterial.alphaMode == "MASK")
        {
            material.alphaMode = Model::AlphaMode::Mask;
        }
        else if (srcMaterial.alphaMode == "BLEND")
        {
            material.alphaMode = Model::AlphaMode::Blend;
        }
        else
        {
            material.alphaMode = Model::AlphaMode::Opaque;
        }

        material.baseColorTextureIndex = srcMaterial.pbrMetallicRoughness.baseColorTexture.index;
        material.normalTextureIndex = srcMaterial.normalTexture.index;

        model.materials.push_back(material);
    }
}

void vulkanGLTF::loadTextures(tinygltf::Model &input, Model &model)
{
    model.images.resize(input.images.size());
    for (size_t i = 0; i < input.images.size(); ++i)
    {
        // GPU handles remain null until a dedicated texture upload path is implemented
        model.images[i].gpu = TextureGPU{};
    }

    model.textures.resize(input.textures.size());
    for (size_t i = 0; i < input.textures.size(); ++i)
    {
        model.textures[i].imageIndex = input.textures[i].source;
    }
}

void vulkanGLTF::createTextureImage(TextureGPU &texture,
                                     const void *data,
                                     VkDeviceSize size,
                                     uint32_t width,
                                     uint32_t height,
                                     VkFormat format)
{
    (void)texture;
    (void)data;
    (void)size;
    (void)width;
    (void)height;
    (void)format;
    // Placeholder: Actual texture upload path not implemented in this loader refresh.
}

int vulkanGLTF::loadglTFFile(std::string filename)
{
    if (!device)
    {
        logs("[!] vulkanGLTF::loadglTFFile called without a valid VulkanDevice");
        return -1;
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model gltfModel;
    std::string err;
    std::string warn;

    std::string extension = getFileExtension(filename);
    bool success = false;

    if (extension == "glb")
    {
        success = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
    }
    else
    {
        success = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);
    }

    if (!warn.empty())
    {
        logs("[GLTF WARN] " << warn);
    }

    if (!success)
    {
        logs("[!] Failed to load GLTF file: " << filename << " | " << err);
        return -1;
    }

    Model model;
    model.name = filename;

    loadTextures(gltfModel, model);
    loadMaterials(gltfModel, model);

    Model::Mesh combinedMesh;

    for (const auto &gltfMesh : gltfModel.meshes)
    {
        for (const auto &primitive : gltfMesh.primitives)
        {
            std::vector<uint32_t> localToUnique;
            loadVertices(gltfModel, primitive, model, localToUnique);

            Primitive outPrim{};
            outPrim.firstIndex = static_cast<uint32_t>(model.indices.size());
            outPrim.materialIndex = primitive.material;

            loadIndices(gltfModel, primitive, model, localToUnique);

            outPrim.indexCount = static_cast<uint32_t>(model.indices.size()) - outPrim.firstIndex;
            combinedMesh.primitives.push_back(outPrim);
        }
    }

    combinedMesh.vertexCount = static_cast<uint32_t>(model.vertices.size());
    combinedMesh.indexCount = static_cast<uint32_t>(model.indices.size());
    model.meshes.push_back(std::move(combinedMesh));

    for (const auto &scene : gltfModel.scenes)
    {
        for (int nodeIndex : scene.nodes)
        {
            if (nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < gltfModel.nodes.size())
            {
                loadNode(gltfModel, gltfModel.nodes[nodeIndex], nullptr, model);
            }
        }
    }

    if (model.nodes.empty())
    {
        // Create a default root node referencing the combined mesh
        auto *root = new Model::Node();
        root->meshIndex = 0;
        root->matrix = glm::mat4(1.0f);
        model.nodes.push_back(root);
    }

    createBuffers(model);

    models.push_back(std::move(model));
    return static_cast<int>(models.size() - 1);
}
