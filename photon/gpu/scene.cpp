#include "scene.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "vulkan_core.h"

#include "scenePostProcess_frag_spv.hpp"
#include "postProcess_vert_spv.hpp"
#include "scene_frag_spv.hpp"
#include "scene_vert_spv.hpp"

namespace {

bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT
        || format == VK_FORMAT_D24_UNORM_S8_UINT
        || format == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkFormat pickDepthFormat(VkPhysicalDevice physicalDevice) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

void destroyTexture(TextureResource& texture, VkDevice device) {
    if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture.sampler, nullptr);
    if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(device, texture.view, nullptr);
    if (texture.image != VK_NULL_HANDLE) vkDestroyImage(device, texture.image, nullptr);
    if (texture.memory != VK_NULL_HANDLE) vkFreeMemory(device, texture.memory, nullptr);
    texture = {};
}

void createBufferResource(GPU& gpu, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(gpu.device, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(gpu.device, buffer, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = gpu.getMemoryType(requirements.memoryTypeBits, properties);
    vkAllocateMemory(gpu.device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(gpu.device, buffer, memory, 0);
}

void createImageResource(GPU& gpu, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
    VkImageAspectFlags aspectMask, VkSampleCountFlagBits samples,
    VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(gpu.device, &imageInfo, nullptr, &image);

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(gpu.device, image, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = gpu.getMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(gpu.device, &allocInfo, nullptr, &memory);
    vkBindImageMemory(gpu.device, image, memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(gpu.device, &viewInfo, nullptr, &view);
}

TextureResource createTexture2DFromRGBA(Scene& scene, GPU& gpu,
    const unsigned char* rgba, uint32_t width, uint32_t height, VkFormat format) {
    TextureResource texture{};
    if (rgba == nullptr || width == 0 || height == 0) return texture;

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBufferResource(gpu, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(gpu.device, stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(gpu.device, stagingMemory);

    VkExtent2D extent{width, height};
    createImageResource(gpu, extent, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, texture.image, texture.memory, texture.view);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = gpu.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(gpu.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    gpu.setImageLayout(commandBuffer, texture.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    gpu.setImageLayout(commandBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(gpu.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gpu.queue);
    vkFreeCommandBuffers(gpu.device, gpu.commandPool, 1, &commandBuffer);

    vkDestroyBuffer(gpu.device, stagingBuffer, nullptr);
    vkFreeMemory(gpu.device, stagingMemory, nullptr);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxAnisotropy = 1.0f;
    vkCreateSampler(gpu.device, &samplerInfo, nullptr, &texture.sampler);
    return texture;
}

void createFallbackTexture(Scene& scene, GPU& gpu) {
    if (scene.fallbackWhiteTexture.image != VK_NULL_HANDLE) return;
    const unsigned char white[4] = {255, 255, 255, 255};
    scene.fallbackWhiteTexture = createTexture2DFromRGBA(scene, gpu, white, 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
}

const TextureResource* pickTexture(const Scene& scene, const SceneObject& object, int textureIndex, bool srgb) {
    const std::vector<TextureResource>& textures = srgb ? object.gltfTexturesSrgb : object.gltfTexturesLinear;
    if (textureIndex >= 0 && textureIndex < static_cast<int>(textures.size()) && textures[textureIndex].view != VK_NULL_HANDLE) {
        return &textures[textureIndex];
    }
    return &scene.fallbackWhiteTexture;
}

void loadObjectTextures(Scene& scene, SceneObject& object, GPU& gpu) {
    if (!object.gltfTexturesSrgb.empty() || object.model.textures.empty()) return;
    object.gltfTexturesSrgb.resize(object.model.textures.size());
    object.gltfTexturesLinear.resize(object.model.textures.size());

    for (size_t i = 0; i < object.model.textures.size(); i++) {
        const tinygltf::Texture& texture = object.model.textures[i];
        if (texture.source < 0 || texture.source >= static_cast<int>(object.model.images.size())) continue;
        const tinygltf::Image& image = object.model.images[texture.source];
        if (image.image.empty() || image.width <= 0 || image.height <= 0) continue;

        std::vector<unsigned char> rgba;
        const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        if (image.component == 4) {
            rgba = image.image;
        } else if (image.component == 3) {
            rgba.resize(pixelCount * 4);
            for (size_t pixel = 0; pixel < pixelCount; pixel++) {
                rgba[pixel * 4 + 0] = image.image[pixel * 3 + 0];
                rgba[pixel * 4 + 1] = image.image[pixel * 3 + 1];
                rgba[pixel * 4 + 2] = image.image[pixel * 3 + 2];
                rgba[pixel * 4 + 3] = 255;
            }
        } else if (image.component == 1) {
            rgba.resize(pixelCount * 4);
            for (size_t pixel = 0; pixel < pixelCount; pixel++) {
                rgba[pixel * 4 + 0] = image.image[pixel];
                rgba[pixel * 4 + 1] = image.image[pixel];
                rgba[pixel * 4 + 2] = image.image[pixel];
                rgba[pixel * 4 + 3] = 255;
            }
        } else {
            continue;
        }

        object.gltfTexturesSrgb[i] = createTexture2DFromRGBA(scene, gpu, rgba.data(),
            static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), VK_FORMAT_R8G8B8A8_SRGB);
        object.gltfTexturesLinear[i] = createTexture2DFromRGBA(scene, gpu, rgba.data(),
            static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), VK_FORMAT_R8G8B8A8_UNORM);
    }
}

void createObjectVertexBuffer(SceneObject& object, GPU& gpu) {
    if (object.vertices.empty()) return;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(object.vertices.size() * sizeof(GltfVertex));

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBufferResource(gpu, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(gpu.device, stagingMemory, 0, bufferSize, 0, &mapped);
    std::memcpy(mapped, object.vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(gpu.device, stagingMemory);

    createBufferResource(gpu, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, object.vertexBuffer, object.vertexBufferMemory);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = gpu.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(gpu.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copy{};
    copy.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, object.vertexBuffer, 1, &copy);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(gpu.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gpu.queue);
    vkFreeCommandBuffers(gpu.device, gpu.commandPool, 1, &commandBuffer);

    vkDestroyBuffer(gpu.device, stagingBuffer, nullptr);
    vkFreeMemory(gpu.device, stagingMemory, nullptr);
}

void createObjectMaterialResources(Scene& scene, SceneObject& object, GPU& gpu) {
    if (object.materials.empty()) object.materials.push_back(MaterialRuntime{});

    object.materialUniformBuffers.resize(object.materials.size(), VK_NULL_HANDLE);
    object.materialUniformMemories.resize(object.materials.size(), VK_NULL_HANDLE);
    object.materialDescriptorSets.resize(object.materials.size(), VK_NULL_HANDLE);

    std::vector<VkDescriptorSetLayout> layouts(object.materials.size(), scene.materialDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = scene.internalDescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(gpu.device, &allocInfo, object.materialDescriptorSets.data());

    for (size_t i = 0; i < object.materials.size(); i++) {
        createBufferResource(gpu, sizeof(MaterialParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            object.materialUniformBuffers[i], object.materialUniformMemories[i]);

        void* mapped = nullptr;
        vkMapMemory(gpu.device, object.materialUniformMemories[i], 0, sizeof(MaterialParams), 0, &mapped);
        std::memcpy(mapped, &object.materials[i].params, sizeof(MaterialParams));
        vkUnmapMemory(gpu.device, object.materialUniformMemories[i]);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = object.materialUniformBuffers[i];
        bufferInfo.range = sizeof(MaterialParams);

        const TextureResource* baseColor = pickTexture(scene, object, object.materials[i].baseColorTextureIndex, true);
        const TextureResource* metallicRoughness = pickTexture(scene, object, object.materials[i].metallicRoughnessTextureIndex, false);
        const TextureResource* normal = pickTexture(scene, object, object.materials[i].normalTextureIndex, false);
        const TextureResource* occlusion = pickTexture(scene, object, object.materials[i].occlusionTextureIndex, false);
        const TextureResource* emissive = pickTexture(scene, object, object.materials[i].emissiveTextureIndex, true);

        VkDescriptorImageInfo imageInfos[5]{};
        imageInfos[0] = {baseColor->sampler, baseColor->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {metallicRoughness->sampler, metallicRoughness->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {normal->sampler, normal->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[3] = {occlusion->sampler, occlusion->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[4] = {emissive->sampler, emissive->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkWriteDescriptorSet writes[6]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = object.materialDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        for (uint32_t binding = 1; binding <= 5; binding++) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = object.materialDescriptorSets[i];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[binding].pImageInfo = &imageInfos[binding - 1];
        }
        vkUpdateDescriptorSets(gpu.device, 6, writes, 0, nullptr);
    }
}

void destroyFrame(Scene& scene, uint32_t index) {
    if (index >= scene.frames.size()) return;
    SceneFrame& frame = scene.frames[index];
    const VkDescriptorSet descriptorSet = frame.descriptorSet;
    const VkDescriptorSet frameDescriptorSet = frame.frameDescriptorSet;
    const VkDescriptorSet postDescriptorSet = frame.postDescriptorSet;
    const VkExtent2D extent = frame.extent;
    if (frame.sceneFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(scene.device, frame.sceneFramebuffer, nullptr);
    if (frame.postFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(scene.device, frame.postFramebuffer, nullptr);
    if (frame.sceneMsaaColorView != VK_NULL_HANDLE) vkDestroyImageView(scene.device, frame.sceneMsaaColorView, nullptr);
    if (frame.sceneMsaaColorImage != VK_NULL_HANDLE) vkDestroyImage(scene.device, frame.sceneMsaaColorImage, nullptr);
    if (frame.sceneMsaaColorMemory != VK_NULL_HANDLE) vkFreeMemory(scene.device, frame.sceneMsaaColorMemory, nullptr);
    if (frame.sceneColorView != VK_NULL_HANDLE) vkDestroyImageView(scene.device, frame.sceneColorView, nullptr);
    if (frame.sceneColorImage != VK_NULL_HANDLE) vkDestroyImage(scene.device, frame.sceneColorImage, nullptr);
    if (frame.sceneColorMemory != VK_NULL_HANDLE) vkFreeMemory(scene.device, frame.sceneColorMemory, nullptr);
    if (frame.sceneDepthView != VK_NULL_HANDLE) vkDestroyImageView(scene.device, frame.sceneDepthView, nullptr);
    if (frame.sceneDepthImage != VK_NULL_HANDLE) vkDestroyImage(scene.device, frame.sceneDepthImage, nullptr);
    if (frame.sceneDepthMemory != VK_NULL_HANDLE) vkFreeMemory(scene.device, frame.sceneDepthMemory, nullptr);
    if (frame.outputView != VK_NULL_HANDLE) vkDestroyImageView(scene.device, frame.outputView, nullptr);
    if (frame.outputImage != VK_NULL_HANDLE) vkDestroyImage(scene.device, frame.outputImage, nullptr);
    if (frame.outputMemory != VK_NULL_HANDLE) vkFreeMemory(scene.device, frame.outputMemory, nullptr);
    if (frame.uniformMapped != nullptr && frame.uniformMemory != VK_NULL_HANDLE) vkUnmapMemory(scene.device, frame.uniformMemory);
    if (frame.uniformBuffer != VK_NULL_HANDLE) vkDestroyBuffer(scene.device, frame.uniformBuffer, nullptr);
    if (frame.uniformMemory != VK_NULL_HANDLE) vkFreeMemory(scene.device, frame.uniformMemory, nullptr);
    frame = {};
    frame.descriptorSet = descriptorSet;
    frame.frameDescriptorSet = frameDescriptorSet;
    frame.postDescriptorSet = postDescriptorSet;
    frame.extent = extent;
}

void initFrame(Scene& scene, GPU& gpu, uint32_t index) {
    if (index >= scene.frames.size()) return;
    SceneFrame& frame = scene.frames[index];

    const VkImageAspectFlags depthAspect = hasStencilComponent(scene.sceneDepthFormat)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;

    createImageResource(gpu, frame.extent, scene.sceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, scene.msaaSamples,
        frame.sceneMsaaColorImage, frame.sceneMsaaColorMemory, frame.sceneMsaaColorView);
    createImageResource(gpu, frame.extent, scene.sceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
        frame.sceneColorImage, frame.sceneColorMemory, frame.sceneColorView);
    createImageResource(gpu, frame.extent, scene.sceneDepthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        depthAspect, scene.msaaSamples,
        frame.sceneDepthImage, frame.sceneDepthMemory, frame.sceneDepthView);
    createImageResource(gpu, frame.extent, scene.sceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
        frame.outputImage, frame.outputMemory, frame.outputView);

    VkImageView sceneAttachments[3] = {frame.sceneMsaaColorView, frame.sceneDepthView, frame.sceneColorView};
    VkFramebufferCreateInfo sceneFramebufferInfo{};
    sceneFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    sceneFramebufferInfo.renderPass = scene.renderPass;
    sceneFramebufferInfo.attachmentCount = 3;
    sceneFramebufferInfo.pAttachments = sceneAttachments;
    sceneFramebufferInfo.width = frame.extent.width;
    sceneFramebufferInfo.height = frame.extent.height;
    sceneFramebufferInfo.layers = 1;
    vkCreateFramebuffer(gpu.device, &sceneFramebufferInfo, nullptr, &frame.sceneFramebuffer);

    VkFramebufferCreateInfo postFramebufferInfo{};
    postFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    postFramebufferInfo.renderPass = scene.postRenderPass;
    postFramebufferInfo.attachmentCount = 1;
    postFramebufferInfo.pAttachments = &frame.outputView;
    postFramebufferInfo.width = frame.extent.width;
    postFramebufferInfo.height = frame.extent.height;
    postFramebufferInfo.layers = 1;
    vkCreateFramebuffer(gpu.device, &postFramebufferInfo, nullptr, &frame.postFramebuffer);

    createBufferResource(gpu, sizeof(SceneViewProjection), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        frame.uniformBuffer, frame.uniformMemory);
    vkMapMemory(gpu.device, frame.uniformMemory, 0, sizeof(SceneViewProjection), 0, &frame.uniformMapped);

    if (frame.frameDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = scene.internalDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &scene.uniformDescriptorSetLayout;
        vkAllocateDescriptorSets(gpu.device, &allocInfo, &frame.frameDescriptorSet);
    }
    if (frame.postDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = scene.internalDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &scene.postDescriptorSetLayout;
        vkAllocateDescriptorSets(gpu.device, &allocInfo, &frame.postDescriptorSet);
    }
    if (frame.descriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = scene.descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &scene.descriptorSetLayout;
        vkAllocateDescriptorSets(gpu.device, &allocInfo, &frame.descriptorSet);
    }

    VkDescriptorBufferInfo frameBufferInfo{};
    frameBufferInfo.buffer = frame.uniformBuffer;
    frameBufferInfo.range = sizeof(SceneViewProjection);
    VkWriteDescriptorSet frameWrite{};
    frameWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    frameWrite.dstSet = frame.frameDescriptorSet;
    frameWrite.dstBinding = 0;
    frameWrite.descriptorCount = 1;
    frameWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameWrite.pBufferInfo = &frameBufferInfo;
    vkUpdateDescriptorSets(gpu.device, 1, &frameWrite, 0, nullptr);

    VkDescriptorImageInfo postImageInfo{};
    postImageInfo.sampler = scene.offscreenColorSampler;
    postImageInfo.imageView = frame.sceneColorView;
    postImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet postWrite{};
    postWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    postWrite.dstSet = frame.postDescriptorSet;
    postWrite.dstBinding = 0;
    postWrite.descriptorCount = 1;
    postWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    postWrite.pImageInfo = &postImageInfo;
    vkUpdateDescriptorSets(gpu.device, 1, &postWrite, 0, nullptr);

    VkDescriptorImageInfo outputImageInfo{};
    outputImageInfo.sampler = scene.offscreenColorSampler;
    outputImageInfo.imageView = frame.outputView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet outputWrite{};
    outputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputWrite.dstSet = frame.descriptorSet;
    outputWrite.dstBinding = 0;
    outputWrite.descriptorCount = 1;
    outputWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    outputWrite.pImageInfo = &outputImageInfo;
    vkUpdateDescriptorSets(gpu.device, 1, &outputWrite, 0, nullptr);

    frame.texture = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frame.descriptorSet));
}

glm::vec3 readColorValue(const uint8_t* colorData, size_t colorStride,
    int colorType, int colorComponentType, bool colorNormalized, bool hasColor, uint32_t vertexIndex) {
    if (!hasColor || colorData == nullptr) return glm::vec3(1.0f);
    const uint8_t* p = colorData + vertexIndex * colorStride;
    const int componentCount = tinygltf::GetNumComponentsInType(colorType);
    if (componentCount < 3) return glm::vec3(1.0f);

    glm::vec3 color(1.0f);
    if (colorComponentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        const float* values = reinterpret_cast<const float*>(p);
        color.r = values[0];
        color.g = values[1];
        color.b = values[2];
    } else if (colorComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const uint8_t* values = reinterpret_cast<const uint8_t*>(p);
        color.r = colorNormalized ? values[0] / 255.0f : static_cast<float>(values[0]);
        color.g = colorNormalized ? values[1] / 255.0f : static_cast<float>(values[1]);
        color.b = colorNormalized ? values[2] / 255.0f : static_cast<float>(values[2]);
    } else if (colorComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* values = reinterpret_cast<const uint16_t*>(p);
        color.r = colorNormalized ? values[0] / 65535.0f : static_cast<float>(values[0]);
        color.g = colorNormalized ? values[1] / 65535.0f : static_cast<float>(values[1]);
        color.b = colorNormalized ? values[2] / 65535.0f : static_cast<float>(values[2]);
    }
    return color;
}

void emitPrimitiveVertex(const uint8_t* posData, size_t posStride, const uint8_t* normalData,
    size_t normalStride, bool hasNormals, const uint8_t* uvData, size_t uvStride, bool hasUV0,
    const uint8_t* colorData, size_t colorStride, int colorType, int colorComponentType,
    bool colorNormalized, bool hasColor, uint32_t vertexIndex, const glm::mat4& worldMatrix,
    std::vector<GltfVertex>& outVertices) {
    const float* p = reinterpret_cast<const float*>(posData + vertexIndex * posStride);
    const glm::vec4 worldPos = worldMatrix * glm::vec4(p[0], p[1], p[2], 1.0f);
    const glm::vec3 color = readColorValue(colorData, colorStride, colorType, colorComponentType, colorNormalized, hasColor, vertexIndex);

    glm::vec3 normal(0.0f, 0.0f, 1.0f);
    if (hasNormals && normalData != nullptr) {
        const float* n = reinterpret_cast<const float*>(normalData + vertexIndex * normalStride);
        normal = glm::normalize(glm::mat3(worldMatrix) * glm::vec3(n[0], n[1], n[2]));
    }

    glm::vec2 uv(0.0f);
    if (hasUV0 && uvData != nullptr) {
        const float* uvValues = reinterpret_cast<const float*>(uvData + vertexIndex * uvStride);
        uv = glm::vec2(uvValues[0], uvValues[1]);
    }

    GltfVertex vertex{};
    vertex.pos[0] = worldPos.x;
    vertex.pos[1] = worldPos.y;
    vertex.pos[2] = worldPos.z;
    vertex.color[0] = color.r;
    vertex.color[1] = color.g;
    vertex.color[2] = color.b;
    vertex.uv[0] = uv.x;
    vertex.uv[1] = uv.y;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    outVertices.push_back(vertex);
}

glm::mat4 nodeLocalMatrix(const tinygltf::Node& node) {
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

        matrix = glm::translate(glm::mat4(1.0f), translation)
            * glm::mat4_cast(rotation)
            * glm::scale(glm::mat4(1.0f), scale);
    }
    return matrix;
}

void appendPrimitiveVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
    const glm::mat4& worldMatrix, std::vector<GltfVertex>& outVertices, std::vector<PrimitiveRange>& outRanges) {
    const auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) return;

    const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
    if (posAccessor.bufferView < 0 || static_cast<size_t>(posAccessor.bufferView) >= model.bufferViews.size()) return;
    const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
    if (posView.buffer < 0 || static_cast<size_t>(posView.buffer) >= model.buffers.size()) return;
    const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
    if (posView.byteOffset + posAccessor.byteOffset >= posBuffer.data.size()) return;

    const uint8_t* posData = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;
    const size_t posStride = posAccessor.ByteStride(posView) ? posAccessor.ByteStride(posView) : sizeof(float) * 3;

    bool hasNormals = false;
    const uint8_t* normalData = nullptr;
    size_t normalStride = 0;
    const auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end()) {
        const tinygltf::Accessor& normalAccessor = model.accessors[normalIt->second];
        if (normalAccessor.bufferView >= 0 && static_cast<size_t>(normalAccessor.bufferView) < model.bufferViews.size()) {
            const tinygltf::BufferView& normalView = model.bufferViews[normalAccessor.bufferView];
            if (normalView.buffer >= 0 && static_cast<size_t>(normalView.buffer) < model.buffers.size()) {
                const tinygltf::Buffer& normalBuffer = model.buffers[normalView.buffer];
                if (normalView.byteOffset + normalAccessor.byteOffset < normalBuffer.data.size()) {
                    normalData = normalBuffer.data.data() + normalView.byteOffset + normalAccessor.byteOffset;
                    normalStride = normalAccessor.ByteStride(normalView) ? normalAccessor.ByteStride(normalView) : sizeof(float) * 3;
                    hasNormals = true;
                }
            }
        }
    }

    bool hasUV0 = false;
    const uint8_t* uvData = nullptr;
    size_t uvStride = 0;
    const auto uvIt = primitive.attributes.find("TEXCOORD_0");
    if (uvIt != primitive.attributes.end()) {
        const tinygltf::Accessor& uvAccessor = model.accessors[uvIt->second];
        if (uvAccessor.bufferView >= 0 && static_cast<size_t>(uvAccessor.bufferView) < model.bufferViews.size()) {
            const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
            if (uvView.buffer >= 0 && static_cast<size_t>(uvView.buffer) < model.buffers.size()) {
                const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
                if (uvView.byteOffset + uvAccessor.byteOffset < uvBuffer.data.size()) {
                    uvData = uvBuffer.data.data() + uvView.byteOffset + uvAccessor.byteOffset;
                    uvStride = uvAccessor.ByteStride(uvView) ? uvAccessor.ByteStride(uvView) : sizeof(float) * 2;
                    hasUV0 = true;
                }
            }
        }
    }

    bool hasColor = false;
    const uint8_t* colorData = nullptr;
    size_t colorStride = 0;
    int colorType = TINYGLTF_TYPE_VEC3;
    int colorComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    bool colorNormalized = false;
    const auto colorIt = primitive.attributes.find("COLOR_0");
    if (colorIt != primitive.attributes.end()) {
        const tinygltf::Accessor& colorAccessor = model.accessors[colorIt->second];
        if (colorAccessor.bufferView >= 0 && static_cast<size_t>(colorAccessor.bufferView) < model.bufferViews.size()) {
            const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
            if (colorView.buffer >= 0 && static_cast<size_t>(colorView.buffer) < model.buffers.size()) {
                const tinygltf::Buffer& colorBuffer = model.buffers[colorView.buffer];
                if (colorView.byteOffset + colorAccessor.byteOffset < colorBuffer.data.size()) {
                    colorData = colorBuffer.data.data() + colorView.byteOffset + colorAccessor.byteOffset;
                    colorStride = colorAccessor.ByteStride(colorView)
                        ? colorAccessor.ByteStride(colorView)
                        : tinygltf::GetNumComponentsInType(colorAccessor.type)
                            * tinygltf::GetComponentSizeInBytes(colorAccessor.componentType);
                    colorType = colorAccessor.type;
                    colorComponentType = colorAccessor.componentType;
                    colorNormalized = colorAccessor.normalized;
                    hasColor = true;
                }
            }
        }
    }

    const uint32_t firstVertex = static_cast<uint32_t>(outVertices.size());
    if (primitive.indices < 0) {
        for (uint32_t i = 0; i < posAccessor.count; i++) {
            emitPrimitiveVertex(posData, posStride, normalData, normalStride, hasNormals,
                uvData, uvStride, hasUV0, colorData, colorStride, colorType, colorComponentType,
                colorNormalized, hasColor, i, worldMatrix, outVertices);
        }
    } else {
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        if (indexAccessor.bufferView < 0 || static_cast<size_t>(indexAccessor.bufferView) >= model.bufferViews.size()) return;
        const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
        if (indexView.buffer < 0 || static_cast<size_t>(indexView.buffer) >= model.buffers.size()) return;
        const tinygltf::Buffer& indexBuffer = model.buffers[indexView.buffer];
        if (indexView.byteOffset + indexAccessor.byteOffset >= indexBuffer.data.size()) return;

        const uint8_t* indexData = indexBuffer.data.data() + indexView.byteOffset + indexAccessor.byteOffset;
        const size_t indexStride = indexAccessor.ByteStride(indexView)
            ? indexAccessor.ByteStride(indexView)
            : tinygltf::GetComponentSizeInBytes(indexAccessor.componentType);

        for (uint32_t i = 0; i < indexAccessor.count; i++) {
            const uint8_t* p = indexData + i * indexStride;
            uint32_t index = 0;
            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) index = *reinterpret_cast<const uint8_t*>(p);
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) index = *reinterpret_cast<const uint16_t*>(p);
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) index = *reinterpret_cast<const uint32_t*>(p);
            else continue;

            emitPrimitiveVertex(posData, posStride, normalData, normalStride, hasNormals,
                uvData, uvStride, hasUV0, colorData, colorStride, colorType, colorComponentType,
                colorNormalized, hasColor, index, worldMatrix, outVertices);
        }
    }

    const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
    if (vertexCount > 0) outRanges.push_back({firstVertex, vertexCount, primitive.material});
}

void appendNodeMesh(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix,
    std::vector<GltfVertex>& outVertices, std::vector<PrimitiveRange>& outRanges) {
    const tinygltf::Node& node = model.nodes[nodeIndex];
    const glm::mat4 world = parentMatrix * nodeLocalMatrix(node);
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

void loadObject(SceneObject& object) {
    object.loader = tinygltf::TinyGLTF{};
    object.model = tinygltf::Model{};
    object.vertices.clear();
    object.drawItems.clear();
    object.materials.clear();
    object.loaded = false;

    if (object.source.empty()) return;

    std::string err;
    std::string warn;
    if (!object.loader.LoadBinaryFromMemory(&object.model, &err, &warn, object.source.data(),
            static_cast<unsigned int>(object.source.size()), "assets/models")) {
        return;
    }
    if (object.model.scenes.empty()) return;

    const int sceneIndex = object.model.defaultScene >= 0 ? object.model.defaultScene : 0;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(object.model.scenes.size())) return;

    std::vector<PrimitiveRange> ranges;
    const tinygltf::Scene& scene = object.model.scenes[sceneIndex];
    for (int nodeIndex : scene.nodes) {
        appendNodeMesh(object.model, nodeIndex, glm::mat4(1.0f), object.vertices, ranges);
    }
    if (object.vertices.empty()) return;

    object.materials.reserve(object.model.materials.size() + 1);
    for (const tinygltf::Material& material : object.model.materials) {
        MaterialRuntime runtime{};
        if (material.values.find("baseColorFactor") != material.values.end()) {
            const tinygltf::ColorValue factor = material.values.at("baseColorFactor").ColorFactor();
            runtime.params.baseColorFactor = glm::vec4(
                static_cast<float>(factor[0]), static_cast<float>(factor[1]),
                static_cast<float>(factor[2]), static_cast<float>(factor[3]));
        }
        if (material.values.find("baseColorTexture") != material.values.end()) {
            runtime.baseColorTextureIndex = material.values.at("baseColorTexture").TextureIndex();
            runtime.params.hasBaseColorTexture = runtime.baseColorTextureIndex >= 0 ? 1 : 0;
        }
        if (material.values.find("metallicFactor") != material.values.end()) {
            runtime.params.metallicFactor = static_cast<float>(material.values.at("metallicFactor").Factor());
        }
        if (material.values.find("roughnessFactor") != material.values.end()) {
            runtime.params.roughnessFactor = static_cast<float>(material.values.at("roughnessFactor").Factor());
        }
        if (material.values.find("metallicRoughnessTexture") != material.values.end()) {
            runtime.metallicRoughnessTextureIndex = material.values.at("metallicRoughnessTexture").TextureIndex();
            runtime.params.hasMetallicRoughnessTexture = runtime.metallicRoughnessTextureIndex >= 0 ? 1 : 0;
        }
        if (material.additionalValues.find("normalTexture") != material.additionalValues.end()) {
            runtime.normalTextureIndex = material.additionalValues.at("normalTexture").TextureIndex();
            runtime.params.hasNormalTexture = runtime.normalTextureIndex >= 0 ? 1 : 0;
        }
        if (material.additionalValues.find("normalScale") != material.additionalValues.end()) {
            runtime.params.normalScale = static_cast<float>(material.additionalValues.at("normalScale").Factor());
        }
        if (material.additionalValues.find("occlusionTexture") != material.additionalValues.end()) {
            runtime.occlusionTextureIndex = material.additionalValues.at("occlusionTexture").TextureIndex();
            runtime.params.hasOcclusionTexture = runtime.occlusionTextureIndex >= 0 ? 1 : 0;
        }
        if (material.additionalValues.find("occlusionStrength") != material.additionalValues.end()) {
            runtime.params.occlusionStrength = static_cast<float>(material.additionalValues.at("occlusionStrength").Factor());
        }
        if (material.additionalValues.find("emissiveTexture") != material.additionalValues.end()) {
            runtime.emissiveTextureIndex = material.additionalValues.at("emissiveTexture").TextureIndex();
            runtime.params.hasEmissiveTexture = runtime.emissiveTextureIndex >= 0 ? 1 : 0;
        }
        if (material.additionalValues.find("emissiveFactor") != material.additionalValues.end()) {
            const tinygltf::ColorValue factor = material.additionalValues.at("emissiveFactor").ColorFactor();
            runtime.params.emissiveFactor = glm::vec4(
                static_cast<float>(factor[0]), static_cast<float>(factor[1]),
                static_cast<float>(factor[2]), 1.0f);
        }
        if (material.additionalValues.find("alphaCutoff") != material.additionalValues.end()) {
            runtime.params.alphaCutoff = static_cast<float>(material.additionalValues.at("alphaCutoff").Factor());
        }
        if (material.additionalValues.find("alphaMode") != material.additionalValues.end()) {
            const std::string mode = material.additionalValues.at("alphaMode").string_value;
            runtime.params.alphaMode = mode == "MASK" ? 1 : mode == "BLEND" ? 2 : 0;
        }
        object.materials.push_back(runtime);
    }
    if (object.materials.empty()) object.materials.push_back(MaterialRuntime{});

    for (const PrimitiveRange& range : ranges) {
        DrawItem item{};
        item.firstVertex = range.firstVertex;
        item.vertexCount = range.vertexCount;
        item.materialIndex = range.materialIndex >= 0 && range.materialIndex < static_cast<int>(object.materials.size())
            ? static_cast<uint32_t>(range.materialIndex)
            : 0u;
        object.drawItems.push_back(item);
    }
    if (object.drawItems.empty()) {
        object.drawItems.push_back({0u, static_cast<uint32_t>(object.vertices.size()), 0u});
    }

    object.loaded = true;
}

glm::vec3 positionToVec3(const Position& position) {
    return glm::vec3(position.x, position.y, position.z);
}

glm::mat4 objectModelMatrix(const SceneObject& object) {
    const glm::mat4 baseRotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::translate(glm::mat4(1.0f), positionToVec3(object.position)) * baseRotation;
}

void computeSceneBounds(const Scene& scene, glm::vec3& outMin, glm::vec3& outMax, bool& hasBounds) {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    hasBounds = false;

    for (const SceneObject& object : scene.objects) {
        if (!object.loaded) continue;
        const glm::mat4 modelMatrix = objectModelMatrix(object);
        for (const GltfVertex& vertex : object.vertices) {
            const glm::vec4 worldPos = modelMatrix * glm::vec4(vertex.pos[0], vertex.pos[1], vertex.pos[2], 1.0f);
            outMin = glm::min(outMin, glm::vec3(worldPos));
            outMax = glm::max(outMax, glm::vec3(worldPos));
            hasBounds = true;
        }
    }
}

glm::vec3 trackedTarget(const Scene& scene) {
    if (scene.trackedObjectIndex >= 0 && scene.trackedObjectIndex < static_cast<int>(scene.objects.size())) {
        return positionToVec3(scene.objects[scene.trackedObjectIndex].position);
    }
    return scene.camera.target;
}

} // namespace

void Scene::addModel(const char* name, const unsigned char* newModel, size_t size, bool isTrackable) {
    SceneObject object{};
    object.name = name != nullptr ? name : "SceneModel";
    object.trackable = isTrackable;
    if (newModel != nullptr && size > 0) {
        object.source.assign(newModel, newModel + size);
    }
    objects.push_back(std::move(object));
    if (isTrackable && trackedObjectIndex < 0) {
        trackedObjectIndex = static_cast<int>(objects.size()) - 1;
    }
}

void Scene::init(GPU& gpu) {
    prepareInit(gpu);
    finishInit(gpu);
}

void Scene::dispatchInit(GPU& gpu) {
    gpuAsyncDispatches.fetch_add(1, std::memory_order_relaxed);
    std::thread([this, &gpu]() {
        AsyncDispatchGuard guard{};
        prepareInit(gpu);
    }).detach();
}

void Scene::prepareInit(GPU& gpu) {
    if (device != VK_NULL_HANDLE) destroy();
    if (objects.empty()) return;

    device = gpu.device;
    physicalDevice = gpu.physicalDevice;
    descriptorPool = gpu.descriptorPool;
    descriptorSetLayout = gpu.descriptorSetLayout;
    frameIndex = &gpu.frameIndex;
    fif = std::max(1u, static_cast<uint32_t>(gpu.swapchainImages.size()));
    msaaSamples = gpu.msaaSamples;
    dirty = false;
    initialized.store(false);
    partInitialized.store(false);

    uint32_t totalMaterials = 0;
    bool anyLoaded = false;
    for (SceneObject& object : objects) {
        loadObject(object);
        if (object.loaded) {
            totalMaterials += std::max<uint32_t>(1u, static_cast<uint32_t>(object.materials.size()));
            anyLoaded = true;
        }
    }
    if (!anyLoaded) return;

    if (trackedObjectIndex < 0) {
        for (size_t i = 0; i < objects.size(); i++) {
            if (objects[i].trackable) {
                trackedObjectIndex = static_cast<int>(i);
                break;
            }
        }
    }

    vertexBindingDescription.binding = 0;
    vertexBindingDescription.stride = sizeof(GltfVertex);
    vertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GltfVertex, pos))};
    vertexAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GltfVertex, color))};
    vertexAttributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GltfVertex, uv))};
    vertexAttributeDescriptions[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GltfVertex, normal))};

    sceneColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    sceneDepthFormat = pickDepthFormat(physicalDevice);
    if (sceneDepthFormat == VK_FORMAT_UNDEFINED) return;

    const uint32_t requiredSetCount = fif * 2 + totalMaterials;
    const uint32_t requiredUniformCount = fif + totalMaterials;
    const uint32_t requiredSamplerCount = fif + totalMaterials * 5;
    const uint32_t setSlack = std::max(32u, requiredSetCount / 8u);
    const uint32_t uniformSlack = std::max(32u, requiredUniformCount / 8u);
    const uint32_t samplerSlack = std::max(64u, requiredSamplerCount / 8u);

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = requiredUniformCount + uniformSlack;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = requiredSamplerCount + samplerSlack;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = requiredSetCount + setSlack;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &internalDescriptorPool);

    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding = 0;
    frameBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo frameLayoutInfo{};
    frameLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameLayoutInfo.bindingCount = 1;
    frameLayoutInfo.pBindings = &frameBinding;
    vkCreateDescriptorSetLayout(device, &frameLayoutInfo, nullptr, &uniformDescriptorSetLayout);

    VkDescriptorSetLayoutBinding materialBindings[6]{};
    materialBindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (uint32_t i = 1; i < 6; i++) {
        materialBindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    materialLayoutInfo.bindingCount = 6;
    materialLayoutInfo.pBindings = materialBindings;
    vkCreateDescriptorSetLayout(device, &materialLayoutInfo, nullptr, &materialDescriptorSetLayout);

    VkDescriptorSetLayoutBinding postBinding{};
    postBinding.binding = 0;
    postBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    postBinding.descriptorCount = 1;
    postBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo postLayoutInfo{};
    postLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    postLayoutInfo.bindingCount = 1;
    postLayoutInfo.pBindings = &postBinding;
    vkCreateDescriptorSetLayout(device, &postLayoutInfo, nullptr, &postDescriptorSetLayout);

    VkAttachmentDescription sceneAttachments[3]{};
    sceneAttachments[0].format = sceneColorFormat;
    sceneAttachments[0].samples = msaaSamples;
    sceneAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    sceneAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    sceneAttachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    sceneAttachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    sceneAttachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sceneAttachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sceneAttachments[1].format = sceneDepthFormat;
    sceneAttachments[1].samples = msaaSamples;
    sceneAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    sceneAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    sceneAttachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    sceneAttachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    sceneAttachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    sceneAttachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    sceneAttachments[2].format = sceneColorFormat;
    sceneAttachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    sceneAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    sceneAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    sceneAttachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    sceneAttachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    sceneAttachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sceneAttachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sceneSubpass{};
    sceneSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sceneSubpass.colorAttachmentCount = 1;
    sceneSubpass.pColorAttachments = &colorRef;
    sceneSubpass.pDepthStencilAttachment = &depthRef;
    sceneSubpass.pResolveAttachments = &resolveRef;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 3;
    renderPassInfo.pAttachments = sceneAttachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &sceneSubpass;
    vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);

    VkAttachmentDescription postAttachment{};
    postAttachment.format = sceneColorFormat;
    postAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    postAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    postAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    postAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    postAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    postAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    postAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference postColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription postSubpass{};
    postSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    postSubpass.colorAttachmentCount = 1;
    postSubpass.pColorAttachments = &postColorRef;
    VkRenderPassCreateInfo postRenderPassInfo{};
    postRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    postRenderPassInfo.attachmentCount = 1;
    postRenderPassInfo.pAttachments = &postAttachment;
    postRenderPassInfo.subpassCount = 1;
    postRenderPassInfo.pSubpasses = &postSubpass;
    vkCreateRenderPass(device, &postRenderPassInfo, nullptr, &postRenderPass);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ScenePushConstants);

    VkDescriptorSetLayout sceneLayouts[2] = {uniformDescriptorSetLayout, materialDescriptorSetLayout};
    VkPipelineLayoutCreateInfo sceneLayoutInfo{};
    sceneLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    sceneLayoutInfo.setLayoutCount = 2;
    sceneLayoutInfo.pSetLayouts = sceneLayouts;
    sceneLayoutInfo.pushConstantRangeCount = 1;
    sceneLayoutInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(device, &sceneLayoutInfo, nullptr, &scenePipelineLayout);

    VkPipelineLayoutCreateInfo postPipelineLayoutInfo{};
    postPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    postPipelineLayoutInfo.setLayoutCount = 1;
    postPipelineLayoutInfo.pSetLayouts = &postDescriptorSetLayout;
    postPipelineLayoutInfo.pushConstantRangeCount = 1;
    postPipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(device, &postPipelineLayoutInfo, nullptr, &postPipelineLayout);

    VkPipelineShaderStageCreateInfo sceneStages[2]{};
    sceneStages[0] = gpu.loadShader(scene_vert_spv, scene_vert_spv_size, sceneVertModule, VK_SHADER_STAGE_VERTEX_BIT, device);
    sceneStages[1] = gpu.loadShader(scene_frag_spv, scene_frag_spv_size, sceneFragModule, VK_SHADER_STAGE_FRAGMENT_BIT, device);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBindingDescription;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = msaaSamples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = sceneStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = scenePipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &scenePipeline);

    VkPipelineShaderStageCreateInfo postStages[2]{};
    postStages[0] = gpu.loadShader(postProcess_vert_spv, postProcess_vert_spv_size, postVertModule, VK_SHADER_STAGE_VERTEX_BIT, device);
    postStages[1] = gpu.loadShader(scenePostProcess_frag_spv, scenePostProcess_frag_spv_size, postFragModule, VK_SHADER_STAGE_FRAGMENT_BIT, device);

    VkPipelineVertexInputStateCreateInfo postVertexInput{};
    postVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineDepthStencilStateCreateInfo postDepthStencil{};
    postDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pStages = postStages;
    pipelineInfo.pVertexInputState = &postVertexInput;
    pipelineInfo.pDepthStencilState = &postDepthStencil;
    pipelineInfo.layout = postPipelineLayout;
    pipelineInfo.renderPass = postRenderPass;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &postPipeline);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    vkCreateSampler(device, &samplerInfo, nullptr, &offscreenColorSampler);

    glm::vec3 minBounds{};
    glm::vec3 maxBounds{};
    bool hasBounds = false;
    computeSceneBounds(*this, minBounds, maxBounds, hasBounds);
    const glm::vec3 center = hasBounds ? (minBounds + maxBounds) * 0.5f : glm::vec3(0.0f);
    const glm::vec3 extents = hasBounds ? (maxBounds - minBounds) : glm::vec3(1.0f);
    const float radius = std::max({extents.x, extents.y, extents.z, 1.0f}) * 0.5f;

    camera.target = trackedObjectIndex >= 0 ? positionToVec3(objects[trackedObjectIndex].position) : center;
    camera.position = camera.target + glm::vec3(radius * 1.75f, 0.0f, 0.0f);
    camera.yaw = 180.0f;
    camera.pitch = -15.0f;
    camera.distance = std::max(radius * 1.75f, 1.0f);
    camera.minDistance = std::max(radius * 0.01f, 0.01f);
    camera.maxDistance = std::max(radius * 24.0f, 32.0f);
    camera.orbitSensitivity = 0.35f;
    camera.panSensitivity = 1.0f;
    camera.zoomSensitivity = 0.050f;
    camera.front = {-1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    partInitialized.store(true);
}

void Scene::finishInit(GPU& gpu) {
    if (initialized.load()) return;
    bool expected = true;
    if (!partInitialized.compare_exchange_strong(expected, false)) return;

    createFallbackTexture(*this, gpu);
    for (SceneObject& object : objects) {
        if (!object.loaded) continue;
        loadObjectTextures(*this, object, gpu);
        createObjectVertexBuffer(object, gpu);
        createObjectMaterialResources(*this, object, gpu);
    }

    frames.assign(fif, {});
    for (uint32_t i = 0; i < fif; i++) {
        initFrame(*this, gpu, i);
    }
    initialized.store(true);
}

void Scene::render(GPU& gpu, VkCommandBuffer& commandBuffer) {
    if (!initialized.load() && partInitialized.load()) finishInit(gpu);
    if (!initialized.load() || frames.empty() || frameIndex == nullptr) return;
    if (dirty) rebuild(gpu);

    SceneFrame& frame = frames[*frameIndex];
    if (frame.sceneFramebuffer == VK_NULL_HANDLE || frame.postFramebuffer == VK_NULL_HANDLE) return;

    const float time = static_cast<float>(ImGui::GetTime());
    const float yawRadians = glm::radians(camera.yaw);
    const float pitchRadians = glm::radians(camera.pitch);

    if (cameraMode == SceneCameraMode::TrackModel) {
        camera.target = trackedTarget(*this);
    }

    const glm::vec3 orbitOffset(
        camera.distance * std::cos(pitchRadians) * std::cos(yawRadians),
        camera.distance * std::cos(pitchRadians) * std::sin(yawRadians),
        camera.distance * std::sin(pitchRadians));
    camera.position = camera.target + orbitOffset;
    camera.front = glm::normalize(camera.target - camera.position);

    SceneViewProjection vp{};
    vp.view = glm::lookAt(camera.position, camera.target, camera.up);
    vp.proj = glm::perspective(glm::radians(93.0f),
        static_cast<float>(frame.extent.width) / static_cast<float>(std::max(1u, frame.extent.height)), 0.001f, 4096.0f);
    vp.proj[1][1] *= -1.0f;
    vp.camPos = glm::vec4(camera.position, 1.0f);
    std::memcpy(frame.uniformMapped, &vp, sizeof(SceneViewProjection));

    VkImageSubresourceRange colorRange{};
    colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.baseMipLevel = 0;
    colorRange.levelCount = 1;
    colorRange.baseArrayLayer = 0;
    colorRange.layerCount = 1;

    VkImageSubresourceRange depthRange{};
    depthRange.aspectMask = hasStencilComponent(sceneDepthFormat)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;
    depthRange.baseMipLevel = 0;
    depthRange.levelCount = 1;
    depthRange.baseArrayLayer = 0;
    depthRange.layerCount = 1;

    gpu.setImageLayout(commandBuffer, frame.sceneMsaaColorImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        colorRange, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    gpu.setImageLayout(commandBuffer, frame.sceneColorImage,
        frame.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorRange,
        frame.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    gpu.setImageLayout(commandBuffer, frame.sceneDepthImage,
        frame.initialized ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthRange,
        frame.initialized ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    gpu.setImageLayout(commandBuffer, frame.outputImage,
        frame.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorRange,
        frame.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue sceneClear[2]{};
    sceneClear[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    sceneClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo sceneBegin{};
    sceneBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    sceneBegin.renderPass = renderPass;
    sceneBegin.framebuffer = frame.sceneFramebuffer;
    sceneBegin.renderArea.extent = frame.extent;
    sceneBegin.clearValueCount = 2;
    sceneBegin.pClearValues = sceneClear;

    vkCmdBeginRenderPass(commandBuffer, &sceneBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);

    VkViewport viewport{};
    viewport.width = static_cast<float>(frame.extent.width);
    viewport.height = static_cast<float>(frame.extent.height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = frame.extent;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout,
        0, 1, &frame.frameDescriptorSet, 0, nullptr);

    for (const SceneObject& object : objects) {
        if (!object.loaded || object.vertexBuffer == VK_NULL_HANDLE) continue;

        ScenePushConstants pushConstants{};
        pushConstants.resolution[0] = static_cast<float>(frame.extent.width);
        pushConstants.resolution[1] = static_cast<float>(frame.extent.height);
        pushConstants.time = time;
        pushConstants.model = objectModelMatrix(object);
        vkCmdPushConstants(commandBuffer, scenePipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(ScenePushConstants), &pushConstants);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &object.vertexBuffer, &offset);

        for (const DrawItem& item : object.drawItems) {
            if (item.materialIndex >= object.materialDescriptorSets.size()) continue;
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout,
                1, 1, &object.materialDescriptorSets[item.materialIndex], 0, nullptr);
            vkCmdDraw(commandBuffer, item.vertexCount, 1, item.firstVertex, 0);
        }
    }
    vkCmdEndRenderPass(commandBuffer);

    gpu.setImageLayout(commandBuffer, frame.sceneColorImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorRange,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    VkClearValue postClear{};
    postClear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo postBegin{};
    postBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    postBegin.renderPass = postRenderPass;
    postBegin.framebuffer = frame.postFramebuffer;
    postBegin.renderArea.extent = frame.extent;
    postBegin.clearValueCount = 1;
    postBegin.pClearValues = &postClear;

    vkCmdBeginRenderPass(commandBuffer, &postBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    ScenePushConstants postPushConstants{};
    postPushConstants.resolution[0] = static_cast<float>(frame.extent.width);
    postPushConstants.resolution[1] = static_cast<float>(frame.extent.height);
    postPushConstants.time = time;
    vkCmdPushConstants(commandBuffer, postPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(ScenePushConstants), &postPushConstants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout,
        0, 1, &frame.postDescriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    gpu.setImageLayout(commandBuffer, frame.outputImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorRange,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    frame.initialized = true;
}

void Scene::rebuild(GPU& gpu) {
    if (!initialized.load() || frames.empty() || frameIndex == nullptr) return;
    const uint32_t index = *frameIndex;
    destroyFrame(*this, index);
    initFrame(*this, gpu, index);
    dirty = false;
}

void Scene::destroy() {
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < frames.size(); i++) {
        const VkDescriptorSet descriptorSet = frames[i].descriptorSet;
        destroyFrame(*this, i);
        if (descriptorSet != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
        }
    }
    frames.clear();

    for (SceneObject& object : objects) {
        for (size_t i = 0; i < object.materialUniformBuffers.size(); i++) {
            if (object.materialUniformBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(device, object.materialUniformBuffers[i], nullptr);
            if (object.materialUniformMemories[i] != VK_NULL_HANDLE) vkFreeMemory(device, object.materialUniformMemories[i], nullptr);
        }
        object.materialUniformBuffers.clear();
        object.materialUniformMemories.clear();
        object.materialDescriptorSets.clear();

        if (object.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, object.vertexBuffer, nullptr);
        if (object.vertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(device, object.vertexBufferMemory, nullptr);
        object.vertexBuffer = VK_NULL_HANDLE;
        object.vertexBufferMemory = VK_NULL_HANDLE;

        for (TextureResource& texture : object.gltfTexturesSrgb) destroyTexture(texture, device);
        for (TextureResource& texture : object.gltfTexturesLinear) destroyTexture(texture, device);
        object.gltfTexturesSrgb.clear();
        object.gltfTexturesLinear.clear();
        object.loaded = false;
    }

    destroyTexture(fallbackWhiteTexture, device);

    if (offscreenColorSampler != VK_NULL_HANDLE) vkDestroySampler(device, offscreenColorSampler, nullptr);
    if (scenePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, scenePipeline, nullptr);
    if (postPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, postPipeline, nullptr);
    if (scenePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, scenePipelineLayout, nullptr);
    if (postPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, postPipelineLayout, nullptr);
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
    if (postRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, postRenderPass, nullptr);
    if (sceneFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, sceneFragModule, nullptr);
    if (sceneVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, sceneVertModule, nullptr);
    if (postFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, postFragModule, nullptr);
    if (postVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, postVertModule, nullptr);
    if (uniformDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, uniformDescriptorSetLayout, nullptr);
    if (materialDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout, nullptr);
    if (postDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, postDescriptorSetLayout, nullptr);
    if (internalDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, internalDescriptorPool, nullptr);

    offscreenColorSampler = VK_NULL_HANDLE;
    scenePipeline = VK_NULL_HANDLE;
    postPipeline = VK_NULL_HANDLE;
    scenePipelineLayout = VK_NULL_HANDLE;
    postPipelineLayout = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;
    postRenderPass = VK_NULL_HANDLE;
    sceneFragModule = VK_NULL_HANDLE;
    sceneVertModule = VK_NULL_HANDLE;
    postFragModule = VK_NULL_HANDLE;
    postVertModule = VK_NULL_HANDLE;
    uniformDescriptorSetLayout = VK_NULL_HANDLE;
    materialDescriptorSetLayout = VK_NULL_HANDLE;
    postDescriptorSetLayout = VK_NULL_HANDLE;
    internalDescriptorPool = VK_NULL_HANDLE;
    initialized.store(false);
    partInitialized.store(false);
    dirty = false;
    device = VK_NULL_HANDLE;
}
