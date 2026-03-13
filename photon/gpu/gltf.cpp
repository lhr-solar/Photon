#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "gltf.hpp"
#include "gpu.hpp"
#include "imgui.h"
#include "gltf_frag_spv.hpp"
#include "gltf_vert_spv.hpp"
#include "postProcess_frag_spv.hpp"
#include "postProcess_vert_spv.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tinygltf {
bool WriteImageData(const std::string*,
                    const std::string*,
                    Image*,
                    bool,
                    void*) {
    return false;
}
}

namespace {
uint32_t findMemoryTypeLocal(const VkPhysicalDeviceMemoryProperties& memoryProperties, VkMemoryPropertyFlags flags, uint32_t typeFilter) {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

std::vector<GltfVertex> generateSphere(float radius, uint32_t stacks, uint32_t slices) {
    std::vector<GltfVertex> vertices;
    vertices.reserve(static_cast<size_t>(stacks) * static_cast<size_t>(slices) * 6);

    for (uint32_t i = 0; i < stacks; ++i) {
        float theta0 = static_cast<float>(M_PI) * float(i) / float(stacks);
        float theta1 = static_cast<float>(M_PI) * float(i + 1) / float(stacks);

        for (uint32_t j = 0; j < slices; ++j) {
            float phi0 = 2.0f * static_cast<float>(M_PI) * float(j) / float(slices);
            float phi1 = 2.0f * static_cast<float>(M_PI) * float(j + 1) / float(slices);

            float x00 = radius * std::sin(theta0) * std::cos(phi0);
            float y00 = radius * std::cos(theta0);
            float z00 = radius * std::sin(theta0) * std::sin(phi0);

            float x01 = radius * std::sin(theta0) * std::cos(phi1);
            float y01 = radius * std::cos(theta0);
            float z01 = radius * std::sin(theta0) * std::sin(phi1);

            float x10 = radius * std::sin(theta1) * std::cos(phi0);
            float y10 = radius * std::cos(theta1);
            float z10 = radius * std::sin(theta1) * std::sin(phi0);

            float x11 = radius * std::sin(theta1) * std::cos(phi1);
            float y11 = radius * std::cos(theta1);
            float z11 = radius * std::sin(theta1) * std::sin(phi1);

            GltfVertex v00{{x00,y00,z00},{1.0f,1.0f,1.0f},{0.0f,0.0f},{x00,y00,z00}};
            GltfVertex v01{{x01,y01,z01},{1.0f,1.0f,1.0f},{0.0f,0.0f},{x01,y01,z01}};
            GltfVertex v10{{x10,y10,z10},{1.0f,1.0f,1.0f},{0.0f,0.0f},{x10,y10,z10}};
            GltfVertex v11{{x11,y11,z11},{1.0f,1.0f,1.0f},{0.0f,0.0f},{x11,y11,z11}};

            vertices.push_back(v00);
            vertices.push_back(v10);
            vertices.push_back(v11);
            vertices.push_back(v00);
            vertices.push_back(v11);
            vertices.push_back(v01);
        }
    }

    return vertices;
}
}

void GltfModel::initModel(std::string path, VkDevice LogicalDevice, VkQueue& Queue, VkPhysicalDeviceMemoryProperties &MemoryProperties, VkCommandPool& CommandPool, VkExtent2D initialExtent){
    logicalDevice = LogicalDevice;
    queue = Queue;
    memoryProperties = MemoryProperties;
    commandPool = CommandPool;
    extent = initialExtent;
    width = std::max(1u, initialExtent.width);
    height = std::max(1u, initialExtent.height);
    dirty = false;
    initialized = false;
    frameCounter = 0;
    std::cout << "[+] Init GLTF" << std::endl;
    std::string err = {}, warn = {};
    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if(!warn.empty()) std::cout << warn << std::endl;
    if(!err.empty()) std::cout << err << std::endl;
    if (!ok) {
        std::cerr << "[!] Failed to load glTF. Falling back to procedural geometry.\n";
        return;
    }

    int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model.scenes.size())) {
        std::cerr << "[!] glTF has no valid scene. Falling back to procedural geometry.\n";
        return;
    }

    vertices.clear();
    drawItems.clear();
    materials.clear();
    const tinygltf::Scene& scene = model.scenes[sceneIndex];
    std::vector<PrimitiveRange> ranges;
    for (int nodeIndex : scene.nodes) {
        GltfModel::appendNodeMesh(model, nodeIndex, glm::mat4(1.0f), vertices, ranges);
    }

    if (vertices.empty()) {
        std::cerr << "[!] glTF scene has no drawable vertices. Falling back to procedural geometry.\n";
        return;
    }

    glm::vec3 minP(vertices[0].pos[0], vertices[0].pos[1], vertices[0].pos[2]);
    glm::vec3 maxP = minP;
    for (const auto& v : vertices) {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    glm::vec3 center = (minP + maxP) * 0.5f;
    glm::vec3 extents = maxP - minP;
    float maxExtent = std::max(extents.x, std::max(extents.y, extents.z)) * 0.5f;
    float scale = (maxExtent > 0.0f) ? (1.0f / maxExtent) : 1.0f;
    for (auto& v : vertices) {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        p = (p - center) * scale;
        v.pos[0] = p.x;
        v.pos[1] = p.y;
        v.pos[2] = p.z;
    }

    camera.position = {2.5f, 0.0f, 0.0f};
    camera.yaw = 180.0f;
    camera.pitch = 0.0f;
    camera.front = {-1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};

    materials.reserve(model.materials.size() + 1);
    for (const auto& mat : model.materials) {
        MaterialRuntime runtimeMat{};
        if (mat.values.find("baseColorFactor") != mat.values.end()) {
            const auto& cf = mat.values.at("baseColorFactor").ColorFactor();
            runtimeMat.params.baseColorFactor = glm::vec4(
                static_cast<float>(cf[0]),
                static_cast<float>(cf[1]),
                static_cast<float>(cf[2]),
                static_cast<float>(cf[3]));
        }
        if (mat.values.find("baseColorTexture") != mat.values.end()) {
            runtimeMat.baseColorTextureIndex = mat.values.at("baseColorTexture").TextureIndex();
            runtimeMat.params.hasBaseColorTexture = runtimeMat.baseColorTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.values.find("metallicFactor") != mat.values.end()) {
            runtimeMat.params.metallicFactor = static_cast<float>(mat.values.at("metallicFactor").Factor());
        }
        if (mat.values.find("roughnessFactor") != mat.values.end()) {
            runtimeMat.params.roughnessFactor = static_cast<float>(mat.values.at("roughnessFactor").Factor());
        }
        if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
            runtimeMat.metallicRoughnessTextureIndex = mat.values.at("metallicRoughnessTexture").TextureIndex();
            runtimeMat.params.hasMetallicRoughnessTexture = runtimeMat.metallicRoughnessTextureIndex >= 0 ? 1 : 0;
        }

        if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
            runtimeMat.normalTextureIndex = mat.additionalValues.at("normalTexture").TextureIndex();
            runtimeMat.params.hasNormalTexture = runtimeMat.normalTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("normalScale") != mat.additionalValues.end()) {
            runtimeMat.params.normalScale = static_cast<float>(mat.additionalValues.at("normalScale").Factor());
        }
        if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
            runtimeMat.occlusionTextureIndex = mat.additionalValues.at("occlusionTexture").TextureIndex();
            runtimeMat.params.hasOcclusionTexture = runtimeMat.occlusionTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("occlusionStrength") != mat.additionalValues.end()) {
            runtimeMat.params.occlusionStrength = static_cast<float>(mat.additionalValues.at("occlusionStrength").Factor());
        }
        if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
            runtimeMat.emissiveTextureIndex = mat.additionalValues.at("emissiveTexture").TextureIndex();
            runtimeMat.params.hasEmissiveTexture = runtimeMat.emissiveTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end()) {
            const auto& ef = mat.additionalValues.at("emissiveFactor").ColorFactor();
            runtimeMat.params.emissiveFactor = glm::vec4(
                static_cast<float>(ef[0]),
                static_cast<float>(ef[1]),
                static_cast<float>(ef[2]),
                1.0f);
        }
        if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
            runtimeMat.params.alphaCutoff = static_cast<float>(mat.additionalValues.at("alphaCutoff").Factor());
        }
        if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
            const std::string mode = mat.additionalValues.at("alphaMode").string_value;
            if (mode == "MASK") runtimeMat.params.alphaMode = 1;
            else if (mode == "BLEND") runtimeMat.params.alphaMode = 2;
            else runtimeMat.params.alphaMode = 0;
        }
        materials.push_back(runtimeMat);
    }
    if (materials.empty()) {
        materials.push_back(MaterialRuntime{});
    }

    for (const auto& r : ranges) {
        DrawItem d{};
        d.firstVertex = r.firstVertex;
        d.vertexCount = r.vertexCount;
        d.materialIndex = (r.materialIndex >= 0 && r.materialIndex < static_cast<int>(materials.size()))
            ? static_cast<uint32_t>(r.materialIndex)
            : 0;
        drawItems.push_back(d);
    }
    if (drawItems.empty()) {
        drawItems.push_back({0u, static_cast<uint32_t>(vertices.size()), 0u});
    }

    loadGltfTextures();
    createFallbackTexture();

    ready = true;
    std::cout << "[+] Loaded " << vertices.size() << " glTF vertices and " << drawItems.size()
              << " draw items from " << path << std::endl;
};

void GltfModel::deleteGLTF(){
    destroyResources(true, logicalDevice, VK_NULL_HANDLE);
}

void GltfModel::initFrameResources(uint32_t frameCount) {
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = frameCount + static_cast<uint32_t>(materials.size()),
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = static_cast<uint32_t>(5 * std::max<size_t>(1, materials.size()) + 1),
        }
    }};

    VkDescriptorPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = static_cast<uint32_t>(frameCount + std::max<size_t>(1, materials.size()) + 1),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    VK_CHECK(vkCreateDescriptorPool(logicalDevice, &poolCI, nullptr, &internalDescriptorPool));

    frameUniformBuffers.resize(frameCount);
    frameUniformBufferMemory.resize(frameCount);
    frameUniformMapped.resize(frameCount);
    VkBufferCreateInfo bCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(GltfMVP),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    for (uint32_t i = 0; i < frameCount; i++) {
        VkMemoryRequirements req{};
        VK_CHECK(vkCreateBuffer(logicalDevice, &bCI, nullptr, &frameUniformBuffers[i]));
        vkGetBufferMemoryRequirements(logicalDevice, frameUniformBuffers[i], &req);
        VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size,
            .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, req.memoryTypeBits),
        };
        VK_CHECK(vkAllocateMemory(logicalDevice, &alloc, nullptr, &frameUniformBufferMemory[i]));
        VK_CHECK(vkBindBufferMemory(logicalDevice, frameUniformBuffers[i], frameUniformBufferMemory[i], 0));
        VK_CHECK(vkMapMemory(logicalDevice, frameUniformBufferMemory[i], 0, sizeof(GltfMVP), 0, &frameUniformMapped[i]));
    }

    std::vector<VkDescriptorSetLayout> frameLayouts(frameCount, uniformDescriptorSetLayout);
    VkDescriptorSetAllocateInfo frameAlloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = internalDescriptorPool,
        .descriptorSetCount = frameCount,
        .pSetLayouts = frameLayouts.data(),
    };
    frameDescriptorSets.resize(frameCount);
    VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &frameAlloc, frameDescriptorSets.data()));
    for (uint32_t i = 0; i < frameCount; i++) {
        VkDescriptorBufferInfo bi = {
            .buffer = frameUniformBuffers[i],
            .offset = 0,
            .range = sizeof(GltfMVP),
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frameDescriptorSets[i],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bi,
        };
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);
    }

    createMaterialResources();

    std::array<VkDescriptorSetLayout, 1> postLayouts = {postDescriptorSetLayout};
    VkDescriptorSetAllocateInfo postAlloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = internalDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = postLayouts.data(),
    };
    VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &postAlloc, &postDescriptorSet));
    VkDescriptorImageInfo ii = {
        .sampler = offscreenColorSampler,
        .imageView = sceneColorImageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = postDescriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &ii,
    };
    vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);
}

void GltfModel::createResources(VulkanDevice vulkanDevice, VkExtent2D newExtent, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout) {
    if (!ready) {
        return;
    }

    physicalDevice = vulkanDevice.physicalDevice;
    if (newExtent.width > 0 && newExtent.height > 0) {
        extent = newExtent;
        width = newExtent.width;
        height = newExtent.height;
    }

    destroyResources(false, logicalDevice, descriptorPool);

    initShaders();
    createVertexBuffer();
    createImages();
    buildGltfPipeline();
    buildPostPipeline();
    initFrameResources(2);

    if (imguiDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo descriptorAlloc{};
        descriptorAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorAlloc.descriptorPool = descriptorPool;
        descriptorAlloc.descriptorSetCount = 1;
        descriptorAlloc.pSetLayouts = &descriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &descriptorAlloc, &imguiDescriptorSet));
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.sampler = offscreenColorSampler;
    imageDescriptor.imageView = outputImageView;
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = imguiDescriptorSet;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);

    texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(imguiDescriptorSet));
    dirty = false;
    initialized = false;
    frameCounter = 0;
}

void GltfModel::updateBuffers(uint32_t frameIndex, const GltfMVP& mvp) {
    if (frameIndex < frameUniformMapped.size() && frameUniformMapped[frameIndex]) {
        std::memcpy(frameUniformMapped[frameIndex], &mvp, sizeof(GltfMVP));
    }
}

void GltfModel::destroyResources(bool releaseDescriptor, VkDevice deviceHandle, VkDescriptorPool descriptorPool) {
    if (deviceHandle == VK_NULL_HANDLE) {
        return;
    }

    if (releaseDescriptor && imguiDescriptorSet != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(deviceHandle, descriptorPool, 1, &imguiDescriptorSet);
        imguiDescriptorSet = VK_NULL_HANDLE;
        texture = 0;
    }

    if (sceneFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(deviceHandle, sceneFramebuffer, nullptr);
        sceneFramebuffer = VK_NULL_HANDLE;
    }
    if (postFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(deviceHandle, postFramebuffer, nullptr);
        postFramebuffer = VK_NULL_HANDLE;
    }

    if (sceneColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(deviceHandle, sceneColorImageView, nullptr);
        sceneColorImageView = VK_NULL_HANDLE;
    }
    if (sceneColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(deviceHandle, sceneColorImage, nullptr);
        sceneColorImage = VK_NULL_HANDLE;
    }
    if (sceneColorImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(deviceHandle, sceneColorImageMemory, nullptr);
        sceneColorImageMemory = VK_NULL_HANDLE;
    }

    if (sceneDepthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(deviceHandle, sceneDepthImageView, nullptr);
        sceneDepthImageView = VK_NULL_HANDLE;
    }
    if (sceneDepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(deviceHandle, sceneDepthImage, nullptr);
        sceneDepthImage = VK_NULL_HANDLE;
    }
    if (sceneDepthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(deviceHandle, sceneDepthImageMemory, nullptr);
        sceneDepthImageMemory = VK_NULL_HANDLE;
    }

    if (outputImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(deviceHandle, outputImageView, nullptr);
        outputImageView = VK_NULL_HANDLE;
    }
    if (outputImage != VK_NULL_HANDLE) {
        vkDestroyImage(deviceHandle, outputImage, nullptr);
        outputImage = VK_NULL_HANDLE;
    }
    if (outputImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(deviceHandle, outputImageMemory, nullptr);
        outputImageMemory = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < frameUniformBuffers.size(); i++) {
        if (i < frameUniformMapped.size() && frameUniformMapped[i]) {
            vkUnmapMemory(deviceHandle, frameUniformBufferMemory[i]);
        }
        if (frameUniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(deviceHandle, frameUniformBuffers[i], nullptr);
        }
        if (frameUniformBufferMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(deviceHandle, frameUniformBufferMemory[i], nullptr);
        }
    }
    frameUniformBuffers.clear();
    frameUniformBufferMemory.clear();
    frameUniformMapped.clear();
    frameDescriptorSets.clear();

    for (size_t i = 0; i < materialUniformBuffers.size(); i++) {
        if (materialUniformBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(deviceHandle, materialUniformBuffers[i], nullptr);
        if (materialUniformBufferMemory[i] != VK_NULL_HANDLE) vkFreeMemory(deviceHandle, materialUniformBufferMemory[i], nullptr);
    }
    materialUniformBuffers.clear();
    materialUniformBufferMemory.clear();
    materialDescriptorSets.clear();
    postDescriptorSet = VK_NULL_HANDLE;

    if (gltfPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(deviceHandle, gltfPipeline, nullptr);
        gltfPipeline = VK_NULL_HANDLE;
    }
    if (postPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(deviceHandle, postPipeline, nullptr);
        postPipeline = VK_NULL_HANDLE;
    }
    if (gltfPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(deviceHandle, gltfPipelineLayout, nullptr);
        gltfPipelineLayout = VK_NULL_HANDLE;
    }
    if (postPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(deviceHandle, postPipelineLayout, nullptr);
        postPipelineLayout = VK_NULL_HANDLE;
    }
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(deviceHandle, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    if (postRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(deviceHandle, postRenderPass, nullptr);
        postRenderPass = VK_NULL_HANDLE;
    }
    if (offscreenColorSampler != VK_NULL_HANDLE) {
        vkDestroySampler(deviceHandle, offscreenColorSampler, nullptr);
        offscreenColorSampler = VK_NULL_HANDLE;
    }

    if (gltfFragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(deviceHandle, gltfFragModule, nullptr);
        gltfFragModule = VK_NULL_HANDLE;
    }
    if (gltfVertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(deviceHandle, gltfVertModule, nullptr);
        gltfVertModule = VK_NULL_HANDLE;
    }
    if (postFragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(deviceHandle, postFragModule, nullptr);
        postFragModule = VK_NULL_HANDLE;
    }
    if (postVertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(deviceHandle, postVertModule, nullptr);
        postVertModule = VK_NULL_HANDLE;
    }

    if (materialDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(deviceHandle, materialDescriptorSetLayout, nullptr);
        materialDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (postDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(deviceHandle, postDescriptorSetLayout, nullptr);
        postDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (uniformDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(deviceHandle, uniformDescriptorSetLayout, nullptr);
        uniformDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (internalDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(deviceHandle, internalDescriptorPool, nullptr);
        internalDescriptorPool = VK_NULL_HANDLE;
    }

    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(deviceHandle, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(deviceHandle, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }

    if (releaseDescriptor) {
        for (auto& t : gltfTexturesSrgb) destroyTexture(t);
        for (auto& t : gltfTexturesLinear) destroyTexture(t);
        gltfTexturesSrgb.clear();
        gltfTexturesLinear.clear();
        destroyTexture(fallbackWhiteTexture);
    }

    initialized = false;
}

void GltfModel::appendNodeMesh(const tinygltf::Model& model,
                           int nodeIndex,
                           const glm::mat4& parentMatrix,
                           std::vector<GltfVertex>& outVertices,
                           std::vector<PrimitiveRange>& outRanges) {
    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 world = parentMatrix * nodeLocalMatrix(node);
    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (const tinygltf::Primitive& primitive : mesh.primitives) {
            appendPrimitiveVertices(model, primitive, world, outVertices, outRanges);
        }
    }
    for (int child : node.children) {
        appendNodeMesh(model, child, world, outVertices, outRanges);
    }
}

glm::mat4 GltfModel::nodeLocalMatrix(const tinygltf::Node& node) {
    glm::mat4 matrix = glm::mat4(1.0f);
    if (node.matrix.size() == 16) {
        matrix = glm::make_mat4(node.matrix.data());
    } else {
        glm::vec3 translation(0.0f);
        if (node.translation.size() == 3) {
            translation = glm::vec3(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2]));
        }

        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        if (node.rotation.size() == 4) {
            rotation = glm::quat(
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]));
        }

        glm::vec3 scale(1.0f);
        if (node.scale.size() == 3) {
            scale = glm::vec3(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2]));
        }

        matrix = glm::translate(glm::mat4(1.0f), translation) *
                 glm::mat4_cast(rotation) *
                 glm::scale(glm::mat4(1.0f), scale);
    }
    return matrix;
}

void GltfModel::appendPrimitiveVertices(const tinygltf::Model& model,
                                    const tinygltf::Primitive& primitive,
                                    const glm::mat4& worldMatrix,
                                    std::vector<GltfVertex>& outVertices,
                                    std::vector<PrimitiveRange>& outRanges) {
    auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) {
        return;
    }

    const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
    const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
    const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
    const uint8_t* posData = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;
    const size_t posStride = posAccessor.ByteStride(posView) ? posAccessor.ByteStride(posView) : sizeof(float) * 3;

    bool hasNormals = false;
    const uint8_t* normalData = nullptr;
    size_t normalStride = 0;
    auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end()) {
        const tinygltf::Accessor& normalAccessor = model.accessors[normalIt->second];
        const tinygltf::BufferView& normalView = model.bufferViews[normalAccessor.bufferView];
        const tinygltf::Buffer& normalBuffer = model.buffers[normalView.buffer];
        normalData = normalBuffer.data.data() + normalView.byteOffset + normalAccessor.byteOffset;
        normalStride = normalAccessor.ByteStride(normalView) ? normalAccessor.ByteStride(normalView) : sizeof(float) * 3;
        hasNormals = true;
    }

    bool hasUV0 = false;
    const uint8_t* uvData = nullptr;
    size_t uvStride = 0;
    auto uvIt = primitive.attributes.find("TEXCOORD_0");
    if (uvIt != primitive.attributes.end()) {
        const tinygltf::Accessor& uvAccessor = model.accessors[uvIt->second];
        const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
        const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
        uvData = uvBuffer.data.data() + uvView.byteOffset + uvAccessor.byteOffset;
        uvStride = uvAccessor.ByteStride(uvView) ? uvAccessor.ByteStride(uvView) : sizeof(float) * 2;
        hasUV0 = true;
    }

    bool hasColor = false;
    const uint8_t* colorData = nullptr;
    size_t colorStride = 0;
    int colorType = TINYGLTF_TYPE_VEC3;
    int colorComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    bool colorNormalized = false;
    auto colorIt = primitive.attributes.find("COLOR_0");
    if (colorIt != primitive.attributes.end()) {
        const tinygltf::Accessor& colorAccessor = model.accessors[colorIt->second];
        const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
        const tinygltf::Buffer& colorBuffer = model.buffers[colorView.buffer];
        colorData = colorBuffer.data.data() + colorView.byteOffset + colorAccessor.byteOffset;
        colorStride = colorAccessor.ByteStride(colorView) ?
            colorAccessor.ByteStride(colorView) :
            (tinygltf::GetNumComponentsInType(colorAccessor.type) *
             tinygltf::GetComponentSizeInBytes(colorAccessor.componentType));
        colorType = colorAccessor.type;
        colorComponentType = colorAccessor.componentType;
        colorNormalized = colorAccessor.normalized;
        hasColor = true;
    }

    auto readColor = [&](uint32_t vertexIndex) -> glm::vec3 {
        if (!hasColor) {
            return glm::vec3(1.0f);
        }
        const uint8_t* p = colorData + vertexIndex * colorStride;
        const int componentCount = tinygltf::GetNumComponentsInType(colorType);
        glm::vec3 c(1.0f);
        switch (colorComponentType) {
            case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                const float* v = reinterpret_cast<const float*>(p);
                c.r = v[0];
                c.g = v[1];
                c.b = v[2];
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const uint8_t* v = reinterpret_cast<const uint8_t*>(p);
                if (colorNormalized) {
                    c.r = v[0] / 255.0f;
                    c.g = v[1] / 255.0f;
                    c.b = v[2] / 255.0f;
                } else {
                    c.r = static_cast<float>(v[0]);
                    c.g = static_cast<float>(v[1]);
                    c.b = static_cast<float>(v[2]);
                }
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const uint16_t* v = reinterpret_cast<const uint16_t*>(p);
                if (colorNormalized) {
                    c.r = v[0] / 65535.0f;
                    c.g = v[1] / 65535.0f;
                    c.b = v[2] / 65535.0f;
                } else {
                    c.r = static_cast<float>(v[0]);
                    c.g = static_cast<float>(v[1]);
                    c.b = static_cast<float>(v[2]);
                }
                break;
            }
            default:
                break;
        }
        if (componentCount < 3) {
            return glm::vec3(1.0f);
        }
        return c;
    };

    const uint32_t firstVertex = static_cast<uint32_t>(outVertices.size());

    auto emitVertex = [&](uint32_t vertexIndex) {
        const float* p = reinterpret_cast<const float*>(posData + vertexIndex * posStride);
        glm::vec4 worldPos = worldMatrix * glm::vec4(p[0], p[1], p[2], 1.0f);
        glm::vec3 color = readColor(vertexIndex);
        glm::vec3 normal(0.0f, 0.0f, 1.0f);
        if (hasNormals) {
            const float* n = reinterpret_cast<const float*>(normalData + vertexIndex * normalStride);
            normal = glm::normalize(glm::mat3(worldMatrix) * glm::vec3(n[0], n[1], n[2]));
        }
        glm::vec2 uv0(0.0f);
        if (hasUV0) {
            const float* uv = reinterpret_cast<const float*>(uvData + vertexIndex * uvStride);
            uv0 = glm::vec2(uv[0], uv[1]);
        }

        GltfVertex out{};
        out.pos[0] = worldPos.x;
        out.pos[1] = worldPos.y;
        out.pos[2] = worldPos.z;
        out.color[0] = color.r;
        out.color[1] = color.g;
        out.color[2] = color.b;
        out.uv[0] = uv0.x;
        out.uv[1] = uv0.y;
        out.normal[0] = normal.x;
        out.normal[1] = normal.y;
        out.normal[2] = normal.z;
        outVertices.push_back(out);
    };

    if (primitive.indices < 0) {
        for (uint32_t i = 0; i < posAccessor.count; i++) {
            emitVertex(i);
        }
        const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
        if (vertexCount > 0) {
            outRanges.push_back({firstVertex, vertexCount, primitive.material});
        }
        return;
    }

    const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
    const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
    const tinygltf::Buffer& indexBuffer = model.buffers[indexView.buffer];
    const uint8_t* indexData = indexBuffer.data.data() + indexView.byteOffset + indexAccessor.byteOffset;
    const size_t indexStride = indexAccessor.ByteStride(indexView) ?
        indexAccessor.ByteStride(indexView) :
        tinygltf::GetComponentSizeInBytes(indexAccessor.componentType);

    for (uint32_t i = 0; i < indexAccessor.count; i++) {
        const uint8_t* p = indexData + i * indexStride;
        uint32_t index = 0;
        switch (indexAccessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                index = *reinterpret_cast<const uint8_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                index = *reinterpret_cast<const uint16_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                index = *reinterpret_cast<const uint32_t*>(p);
                break;
            default:
                continue;
        }
        emitVertex(index);
    }

    const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
    if (vertexCount > 0) {
        outRanges.push_back({firstVertex, vertexCount, primitive.material});
    }
}

TextureResource GltfModel::createTexture2DFromRGBA(const unsigned char* rgba, uint32_t texWidth, uint32_t texHeight, VkFormat format) {
    TextureResource out{};
    if (!rgba || texWidth == 0 || texHeight == 0) {
        return out;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(logicalDevice, &bufferCI, nullptr, &stagingBuffer));

    VkMemoryRequirements stagingReqs{};
    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingReqs.size,
        .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingReqs.memoryTypeBits),
    };
    VK_CHECK(vkAllocateMemory(logicalDevice, &stagingAlloc, nullptr, &stagingMemory));
    VK_CHECK(vkBindBufferMemory(logicalDevice, stagingBuffer, stagingMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(logicalDevice, stagingMemory, 0, imageSize, 0, &mapped));
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(logicalDevice, stagingMemory);

    VkImageCreateInfo imageCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {texWidth, texHeight, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(logicalDevice, &imageCI, nullptr, &out.image));

    VkMemoryRequirements imageReqs{};
    vkGetImageMemoryRequirements(logicalDevice, out.image, &imageReqs);
    VkMemoryAllocateInfo imageAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = imageReqs.size,
        .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageReqs.memoryTypeBits),
    };
    VK_CHECK(vkAllocateMemory(logicalDevice, &imageAlloc, nullptr, &out.memory));
    VK_CHECK(vkBindImageMemory(logicalDevice, out.image, out.memory, 0));

    VkCommandBufferAllocateInfo cbAlloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &cbAlloc, &cb));
    VkCommandBufferBeginInfo cbBegin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cb, &cbBegin));

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = out.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {texWidth, texHeight, 1};
    vkCmdCopyBufferToImage(cb, stagingBuffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = out.image;
    toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

    VK_CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &cb);

    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingMemory, nullptr);

    VkImageViewCreateInfo viewCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_CHECK(vkCreateImageView(logicalDevice, &viewCI, nullptr, &out.view));

    VkSamplerCreateInfo samplerCI = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 1.0f,
    };
    VK_CHECK(vkCreateSampler(logicalDevice, &samplerCI, nullptr, &out.sampler));
    return out;
}

void GltfModel::destroyTexture(TextureResource& texture) {
    if (texture.sampler) vkDestroySampler(logicalDevice, texture.sampler, nullptr);
    if (texture.view) vkDestroyImageView(logicalDevice, texture.view, nullptr);
    if (texture.image) vkDestroyImage(logicalDevice, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(logicalDevice, texture.memory, nullptr);
    texture = {};
}

void GltfModel::loadGltfTextures() {
    gltfTexturesSrgb.clear();
    gltfTexturesLinear.clear();
    gltfTexturesSrgb.resize(model.textures.size());
    gltfTexturesLinear.resize(model.textures.size());
    for (size_t i = 0; i < model.textures.size(); i++) {
        const tinygltf::Texture& tex = model.textures[i];
        if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size())) {
            continue;
        }
        const tinygltf::Image& image = model.images[tex.source];
        if (image.image.empty() || image.component < 3) {
            continue;
        }

        std::vector<unsigned char> rgba;
        const unsigned char* src = image.image.data();
        if (image.component == 4) {
            rgba.assign(src, src + image.width * image.height * 4);
        } else {
            rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
            for (int p = 0; p < image.width * image.height; p++) {
                rgba[p * 4 + 0] = src[p * image.component + 0];
                rgba[p * 4 + 1] = src[p * image.component + 1];
                rgba[p * 4 + 2] = src[p * image.component + 2];
                rgba[p * 4 + 3] = 255;
            }
        }
        gltfTexturesSrgb[i] = createTexture2DFromRGBA(
            rgba.data(),
            static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height),
            VK_FORMAT_R8G8B8A8_SRGB);
        gltfTexturesLinear[i] = createTexture2DFromRGBA(
            rgba.data(),
            static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height),
            VK_FORMAT_R8G8B8A8_UNORM);
    }
}
    
void GltfModel::createFallbackTexture() {
    const unsigned char white[4] = {255, 255, 255, 255};
    fallbackWhiteTexture = createTexture2DFromRGBA(white, 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
}

void GltfModel::buildGltfPipeline(){
    VkPushConstantRange pcR = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(GltfPushConstants),
    };

    VkDescriptorSetLayoutBinding frameBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo frameDslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &frameBinding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(logicalDevice, &frameDslCI, nullptr, &uniformDescriptorSetLayout));

    std::array<VkDescriptorSetLayoutBinding, 6> materialBindings = {{
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 5,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        }
    }};
    VkDescriptorSetLayoutCreateInfo materialDslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(materialBindings.size()),
        .pBindings = materialBindings.data(),
    };
    VK_CHECK(vkCreateDescriptorSetLayout(logicalDevice, &materialDslCI, nullptr, &materialDescriptorSetLayout));

    std::array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        uniformDescriptorSetLayout,
        materialDescriptorSetLayout
    };
    VkPipelineLayoutCreateInfo plCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(pipelineLayouts.size()),
        .pSetLayouts = pipelineLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcR,
    };
    VK_CHECK(vkCreatePipelineLayout(logicalDevice, &plCI, nullptr, &gltfPipelineLayout));

    viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    scissor = {
        .offset = {0, 0},
        .extent = {width, height}
    };

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = gltfVertModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = gltfFragModule,
            .pName = "main",
        }
    };

    VkPipelineVertexInputStateCreateInfo viCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vAttributeDescriptions.size()),
        .pVertexAttributeDescriptions = vAttributeDescriptions.data(),
    };
    VkPipelineInputAssemblyStateCreateInfo iaCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vpCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo rsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo dsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    std::array<VkDynamicState, 2> dynStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates = dynStates.data(),
    };

    VkGraphicsPipelineCreateInfo gpCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &viCI,
        .pInputAssemblyState = &iaCI,
        .pViewportState = &vpCI,
        .pRasterizationState = &rsCI,
        .pMultisampleState = &msCI,
        .pDepthStencilState = &dsCI,
        .pColorBlendState = &cbCI,
        .pDynamicState = &dynCI,
        .layout = gltfPipelineLayout,
        .renderPass = renderPass,
        .subpass = 0,
    };
    VK_CHECK(vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &gpCI, nullptr, &gltfPipeline));
}
void GltfModel::buildPostPipeline() {
    VkPushConstantRange pcR = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(GltfPushConstants),
    };

    VkDescriptorSetLayoutBinding postBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo postDslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &postBinding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(logicalDevice, &postDslCI, nullptr, &postDescriptorSetLayout));

    VkPipelineLayoutCreateInfo plCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &postDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcR,
    };
    VK_CHECK(vkCreatePipelineLayout(logicalDevice, &plCI, nullptr, &postPipelineLayout));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = postVertModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = postFragModule,
            .pName = "main",
        }
    };

    VkPipelineVertexInputStateCreateInfo viCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };
    VkPipelineInputAssemblyStateCreateInfo iaCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vpCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo rsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo dsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    std::array<VkDynamicState, 2> dynStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates = dynStates.data(),
    };

    VkGraphicsPipelineCreateInfo gpCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &viCI,
        .pInputAssemblyState = &iaCI,
        .pViewportState = &vpCI,
        .pRasterizationState = &rsCI,
        .pMultisampleState = &msCI,
        .pDepthStencilState = &dsCI,
        .pColorBlendState = &cbCI,
        .pDynamicState = &dynCI,
        .layout = postPipelineLayout,
        .renderPass = postRenderPass,
        .subpass = 0,
    };
    VK_CHECK(vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &gpCI, nullptr, &postPipeline));
}

void GltfModel::createMaterialResources() {
    if (materials.empty()) {
        materials.push_back(MaterialRuntime{});
    }

    materialUniformBuffers.resize(materials.size());
    materialUniformBufferMemory.resize(materials.size());
    materialDescriptorSets.resize(materials.size());

    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(MaterialParams),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    for (size_t i = 0; i < materials.size(); i++) {
        VkMemoryRequirements req{};
        VK_CHECK(vkCreateBuffer(logicalDevice, &bufferCI, nullptr, &materialUniformBuffers[i]));
        vkGetBufferMemoryRequirements(logicalDevice, materialUniformBuffers[i], &req);
        VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size,
            .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, req.memoryTypeBits),
        };
        VK_CHECK(vkAllocateMemory(logicalDevice, &alloc, nullptr, &materialUniformBufferMemory[i]));
        VK_CHECK(vkBindBufferMemory(logicalDevice, materialUniformBuffers[i], materialUniformBufferMemory[i], 0));
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(logicalDevice, materialUniformBufferMemory[i], 0, sizeof(MaterialParams), 0, &mapped));
        std::memcpy(mapped, &materials[i].params, sizeof(MaterialParams));
        vkUnmapMemory(logicalDevice, materialUniformBufferMemory[i]);
    }

    std::vector<VkDescriptorSetLayout> layouts(materials.size(), materialDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = internalDescriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data(),
    };
    VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &allocInfo, materialDescriptorSets.data()));

    for (size_t i = 0; i < materials.size(); i++) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = materialUniformBuffers[i],
            .offset = 0,
            .range = sizeof(MaterialParams),
        };

        auto pickTexture = [&](int texIndex, bool srgb) -> const TextureResource* {
            const auto& table = srgb ? gltfTexturesSrgb : gltfTexturesLinear;
            if (texIndex >= 0 && texIndex < static_cast<int>(table.size()) && table[texIndex].view != VK_NULL_HANDLE) {
                return &table[texIndex];
            }
            return &fallbackWhiteTexture;
        };

        const TextureResource* baseColorTex = pickTexture(materials[i].baseColorTextureIndex, true);
        const TextureResource* metallicRoughnessTex = pickTexture(materials[i].metallicRoughnessTextureIndex, false);
        const TextureResource* normalTex = pickTexture(materials[i].normalTextureIndex, false);
        const TextureResource* occlusionTex = pickTexture(materials[i].occlusionTextureIndex, false);
        const TextureResource* emissiveTex = pickTexture(materials[i].emissiveTextureIndex, true);

        std::array<VkDescriptorImageInfo, 5> imageInfos = {{
            { baseColorTex->sampler, baseColorTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { metallicRoughnessTex->sampler, metallicRoughnessTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { normalTex->sampler, normalTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { occlusionTex->sampler, occlusionTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { emissiveTex->sampler, emissiveTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
        }};

        std::array<VkWriteDescriptorSet, 6> writes = {{
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfo,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[0],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[1],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 3,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[2],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 4,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[3],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 5,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[4],
            }
        }};
        vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void GltfModel::recordShaderPass(VkCommandBuffer commandBuffer){
    if (!ready || gltfPipeline == VK_NULL_HANDLE || postPipeline == VK_NULL_HANDLE) {
        return;
    }
    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    width = extent.width;
    height = extent.height;

    const uint32_t frameIndex = frameUniformMapped.empty() ? 0u : (frameCounter++ % static_cast<uint32_t>(frameUniformMapped.size()));
    const float timeSeconds = static_cast<float>(ImGui::GetTime());

    GltfMVP mvp{};
    mvp.model = glm::mat4(1.0f);
    const float orbitRadius = 2.6f;
    const glm::vec3 camPos = glm::vec3(std::cos(timeSeconds * 0.35f) * orbitRadius,
                                       std::sin(timeSeconds * 0.35f) * orbitRadius,
                                       1.2f);
    mvp.view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    mvp.proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 32.0f);
    mvp.proj[1][1] *= -1.0f;
    mvp.camPos = glm::vec4(camPos, 1.0f);
    updateBuffers(frameIndex, mvp);

    VkImageSubresourceRange colorRange{};
    colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.baseMipLevel = 0;
    colorRange.levelCount = 1;
    colorRange.baseArrayLayer = 0;
    colorRange.layerCount = 1;

    VkImageSubresourceRange depthRange{};
    depthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthRange.baseMipLevel = 0;
    depthRange.levelCount = 1;
    depthRange.baseArrayLayer = 0;
    depthRange.layerCount = 1;

    const VkImageLayout oldSceneColorLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    const VkPipelineStageFlags oldSceneColorStage = initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    Gpu::setImageLayout(commandBuffer, sceneColorImage, oldSceneColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            colorRange, oldSceneColorStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    Gpu::setImageLayout(commandBuffer, sceneDepthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            depthRange, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    const VkImageLayout oldOutputLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    const VkPipelineStageFlags oldOutputStage = initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    Gpu::setImageLayout(commandBuffer, outputImage, oldOutputLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            colorRange, oldOutputStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue sceneClear[2] = {0};
    sceneClear[0].color = (VkClearColorValue){{0.00f, 0.00f, 0.00f, 1.0f}};
    sceneClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo sceneRpBI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass,
        .framebuffer = sceneFramebuffer,
        .renderArea = {{0,0},{width, height}},
        .clearValueCount = sizeof(sceneClear)/sizeof(VkClearValue),
        .pClearValues = sceneClear
    };

    VkClearValue postClear = {};
    postClear.color = (VkClearColorValue){{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo postRpBI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = postRenderPass,
        .framebuffer = postFramebuffer,
        .renderArea = {{0,0},{width, height}},
        .clearValueCount = 1,
        .pClearValues = &postClear
    };

    vkCmdBeginRenderPass(commandBuffer, &sceneRpBI, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipeline);
    VkViewport dynViewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D dynScissor = {
        .offset = {0, 0},
        .extent = {width, height},
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &dynViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &dynScissor);

    GltfPushConstants pc = {{static_cast<float>(width), static_cast<float>(height)}, timeSeconds, 0.0f};
    vkCmdPushConstants(commandBuffer, gltfPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GltfPushConstants), &pc);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipelineLayout,
            0, 1, &frameDescriptorSets[frameIndex], 0, nullptr);

    for (const auto& draw : drawItems) {
        if (draw.materialIndex >= materialDescriptorSets.size()) {
            continue;
        }
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipelineLayout,
                1, 1, &materialDescriptorSets[draw.materialIndex], 0, nullptr);
        vkCmdDraw(commandBuffer, draw.vertexCount, 1, draw.firstVertex, 0);
    }

    vkCmdEndRenderPass(commandBuffer);

    Gpu::setImageLayout(commandBuffer, sceneColorImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            colorRange, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkCmdBeginRenderPass(commandBuffer, &postRpBI, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
    vkCmdSetViewport(commandBuffer, 0, 1, &dynViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &dynScissor);
    vkCmdPushConstants(commandBuffer, postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GltfPushConstants), &pc);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout,
            0, 1, &postDescriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    Gpu::setImageLayout(commandBuffer, outputImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            colorRange, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    initialized = true;
}


void GltfModel::createVertexBuffer(){
    std::vector<GltfVertex> uploadVertices = vertices;
    if (uploadVertices.empty()) {
        uploadVertices = generateSphere(1.0, 4, 32);
    }

    const VkDeviceSize bufferSize = uploadVertices.size() * sizeof(GltfVertex);
    if (bufferSize == 0) {
        return;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo stagingCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(logicalDevice, &stagingCI, nullptr, &stagingBuffer));

    VkMemoryRequirements stagingReqs{};
    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingReqs.size,
        .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingReqs.memoryTypeBits),
    };
    VK_CHECK(vkAllocateMemory(logicalDevice, &stagingAlloc, nullptr, &stagingMemory));
    VK_CHECK(vkBindBufferMemory(logicalDevice, stagingBuffer, stagingMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(logicalDevice, stagingMemory, 0, bufferSize, 0, &mapped));
    std::memcpy(mapped, uploadVertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(logicalDevice, stagingMemory);

    VkBufferCreateInfo vbCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(logicalDevice, &vbCI, nullptr, &vertexBuffer));
    VkMemoryRequirements vbReqs{};
    vkGetBufferMemoryRequirements(logicalDevice, vertexBuffer, &vbReqs);
    VkMemoryAllocateInfo vbAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = vbReqs.size,
        .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbReqs.memoryTypeBits),
    };
    VK_CHECK(vkAllocateMemory(logicalDevice, &vbAlloc, nullptr, &vertexBufferMemory));
    VK_CHECK(vkBindBufferMemory(logicalDevice, vertexBuffer, vertexBufferMemory, 0));

    VkCommandBufferAllocateInfo cbAlloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &cbAlloc, &cb));
    VkCommandBufferBeginInfo cbBegin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cb, &cbBegin));
    VkBufferCopy copy = {.size = bufferSize};
    vkCmdCopyBuffer(cb, stagingBuffer, vertexBuffer, 1, &copy);
    VK_CHECK(vkEndCommandBuffer(cb));

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &cb);

    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingMemory, nullptr);
}


void GltfModel::initShaders(){
    VkShaderModuleCreateInfo sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = gltf_frag_spv_size,
        .pCode = gltf_frag_spv,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &gltfFragModule));

    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = gltf_vert_spv_size,
        .pCode = gltf_vert_spv,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &gltfVertModule));

    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = postProcess_frag_spv_size,
        .pCode = postProcess_frag_spv,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &postFragModule));

    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = postProcess_vert_spv_size,
        .pCode = postProcess_vert_spv,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &postVertModule));

    vBindingDescription = {
        .binding = 0,
        .stride = sizeof(struct GltfVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    vAttributeDescriptions = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(struct GltfVertex, pos)
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(struct GltfVertex, color)
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct GltfVertex, uv)
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(struct GltfVertex, normal)
        }
    };
}

void GltfModel::createImages() {
    constexpr VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    VkAttachmentDescription sceneAttachments[2] = {};
    sceneAttachments[0] = {
        .format = colorFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    sceneAttachments[1] = {
        .format = depthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference sceneColorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkAttachmentReference sceneDepthRef = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription sceneSubpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &sceneColorRef,
        .pDepthStencilAttachment = &sceneDepthRef,
    };
    VkRenderPassCreateInfo sceneRpCI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = sceneAttachments,
        .subpassCount = 1,
        .pSubpasses = &sceneSubpass,
    };
    VK_CHECK(vkCreateRenderPass(logicalDevice, &sceneRpCI, nullptr, &renderPass));

    VkAttachmentDescription postColorAttachment = {
        .format = colorFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkAttachmentReference postColorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription postSubpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &postColorRef,
    };
    VkRenderPassCreateInfo postRpCI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &postColorAttachment,
        .subpassCount = 1,
        .pSubpasses = &postSubpass,
    };
    VK_CHECK(vkCreateRenderPass(logicalDevice, &postRpCI, nullptr, &postRenderPass));

    auto createImage = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
        VkImageCreateInfo imageCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VK_CHECK(vkCreateImage(logicalDevice, &imageCI, nullptr, &image));

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(logicalDevice, image, &memReqs);
        VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = findMemoryTypeLocal(memoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memReqs.memoryTypeBits),
        };
        VK_CHECK(vkAllocateMemory(logicalDevice, &alloc, nullptr, &memory));
        VK_CHECK(vkBindImageMemory(logicalDevice, image, memory, 0));

        VkImageViewCreateInfo viewCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {
                .aspectMask = aspectMask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        VK_CHECK(vkCreateImageView(logicalDevice, &viewCI, nullptr, &view));
    };

    createImage(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
            sceneColorImage, sceneColorImageMemory, sceneColorImageView);
    createImage(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
            sceneDepthImage, sceneDepthImageMemory, sceneDepthImageView);
    createImage(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
            outputImage, outputImageMemory, outputImageView);

    VkSamplerCreateInfo samplerCI = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxAnisotropy = 1.0f,
    };
    VK_CHECK(vkCreateSampler(logicalDevice, &samplerCI, nullptr, &offscreenColorSampler));

    std::array<VkImageView, 2> sceneViews = {
        sceneColorImageView,
        sceneDepthImageView
    };
    VkFramebufferCreateInfo sceneFbCI = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = renderPass,
        .attachmentCount = static_cast<uint32_t>(sceneViews.size()),
        .pAttachments = sceneViews.data(),
        .width = width,
        .height = height,
        .layers = 1,
    };
    VK_CHECK(vkCreateFramebuffer(logicalDevice, &sceneFbCI, nullptr, &sceneFramebuffer));

    VkFramebufferCreateInfo postFbCI = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = postRenderPass,
        .attachmentCount = 1,
        .pAttachments = &outputImageView,
        .width = width,
        .height = height,
        .layers = 1,
    };
    VK_CHECK(vkCreateFramebuffer(logicalDevice, &postFbCI, nullptr, &postFramebuffer));
}
