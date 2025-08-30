/*[μ] the photon graphical user interface*/
#include <stdio.h>
#include <string>
#include <cstring>
#include <vulkan/vulkan.h>

#include "vulkan_core.h"
#include "gui.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "../engine/include.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/gpu.hpp"

Gui::Gui(){};
Gui::~Gui(){
    xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
};

void Gui::initWindow(){
    initxcbConnection();
    setupWindow();
}

xcb_visualtype_t* get_argb_visual(xcb_screen_t* screen) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        if (depth_iter.data->depth == 32) {
            xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
            for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
                return visual_iter.data; // Found ARGB visual
            }
        }
    }
    return NULL; // No ARGB visual on this screen
}

void Gui::initxcbConnection(){
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int src;

    connection = xcb_connect(NULL, &src);
    assert(connection);
    if(xcb_connection_has_error(connection)){
        printf("[!] Could not find a compatible Vulkan ICD!\n");
        fflush(stdout);
        exit(1);
    }

    setup = xcb_get_setup(connection);
    iter = xcb_setup_roots_iterator(setup);
    while(src-- > 0)
        xcb_screen_next(&iter);
    screen = iter.data;
}

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
	return xcb_intern_atom_reply(conn, cookie, NULL);
}

void Gui::setupWindow(){
    uint32_t value_mask, value_list[32];

    window = xcb_generate_id(connection);

    xcb_visualtype_t* argb_visual = get_argb_visual(screen);
    xcb_colormap_t colormap = xcb_generate_id(connection);
    xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, argb_visual->visual_id);


	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = screen-> black_pixel;
	value_list[1] =
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;

    if(settings.fullscreen){
        width = screen->width_in_pixels;
		height = screen->height_in_pixels;
    }
    
    xcb_create_window(connection,
		XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list);
    
    xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, (*reply).atom, 4, 32, 1,
		&(*atom_wm_delete_window).atom);

    std::string windowTitle = getWindowTitle();
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		title.size(), windowTitle.c_str());

    free(reply);

    std::string wm_class;
	wm_class = wm_class.insert(0, name);
	wm_class = wm_class.insert(name.size(), 1, '\0');
	wm_class = wm_class.insert(name.size() + 1, title);
	wm_class = wm_class.insert(wm_class.size(), 1, '\0');
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, wm_class.size() + 2, wm_class.c_str());

    if (settings.fullscreen){
        xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
        xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
        xcb_change_property(connection,
            XCB_PROP_MODE_REPLACE,
            window, atom_wm_state->atom,
            XCB_ATOM_ATOM, 32, 1,
            &(atom_wm_fullscreen->atom));
        free(atom_wm_fullscreen);
        free(atom_wm_state);
    }

	xcb_map_window(connection, window);

    xcb_flush(connection);
}

std::string Gui::getWindowTitle() const {
    // TODO: accelerator + fps
	std::string windowTitle =  title ; //+ deviceProperties.deviceName };
    //windowTitle += " - " + std::to_string(frameCounter) + " fps";
	return windowTitle;
}

void Gui::prepareImGui(){
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();
    log("[+] Created ImX context!");

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    // TODO: 
    // Font & System Scale, Styles, Font, etc.
}

// initialize all vulkan resources used by the UI
void Gui::initResources(VulkanDevice vulkanDevice){
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);
    VkImageCreateInfo imageCreateInfo {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.extent.width = texWidth;
    imageCreateInfo.extent.height = texHeight;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(vulkanDevice.logicalDevice, &imageCreateInfo, nullptr, &fontImage);
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, fontImage, &memReqs);
    VkMemoryAllocateInfo memAllocInfo {};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &memAllocInfo, nullptr, &fontMemory));
    log("[+] Allocated font memory");
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, fontImage, fontMemory, 0));
    log("[+] Bound font memory");

    VkImageViewCreateInfo imageViewCreateInfo {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = fontImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &imageViewCreateInfo, nullptr, &fontView));
    log("[+] Created Font Image View");

    VulkanBuffer stagingBuffer;
    VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, uploadSize, nullptr));
    log("[+] Created staging buffer");
    stagingBuffer.map(VK_WHOLE_SIZE, 0);
    memcpy(stagingBuffer.mapped, fontData, uploadSize);
    stagingBuffer.unmap();

    VkCommandBuffer copyCmd = vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vulkanDevice.graphicsCommandPool, true);
    log("[+] Created Command Buffer from Graphics Command Pool");
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;
    Gpu::setImageLayout(copyCmd, fontImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    log("[+] Set Image Layout for Transfer");

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = texWidth;
    bufferCopyRegion.imageExtent.height = texHeight;
    bufferCopyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
    log("[+] Copied Buffer to Image");

    Gpu::setImageLayout(copyCmd, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vulkanDevice.flushCommandBuffer(copyCmd, vulkanDevice.graphicsQueue, vulkanDevice.graphicsCommandPool, true);
    log("[+] Flushed Command Buffer");
    stagingBuffer.destroy();
    log("[+] Destroyed Staging Buffer");

    VkSamplerCreateInfo samplerCreateInfo {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCreateSampler(vulkanDevice.logicalDevice, &samplerCreateInfo, nullptr, &sampler);
    log("[+] Created Gui Sampler");

    // Descriptor Pool
    VkDescriptorPoolSize descriptorPoolSize {};
    descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorPoolSize.descriptorCount = 2;
    std::vector<VkDescriptorPoolSize> poolSizes = { descriptorPoolSize };

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 2;
    VK_CHECK(vkCreateDescriptorPool(vulkanDevice.logicalDevice, &descriptorPoolInfo, nullptr, &guiDescriptorPool));
    log("[+] Created Gui Descriptor Pool");

    // Descriptor set Layout
    VkDescriptorSetLayoutBinding setLayoutBinding0 {};
    setLayoutBinding0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setLayoutBinding0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBinding0.binding = 0;
    setLayoutBinding0.descriptorCount = 1;

    VkDescriptorSetLayoutBinding setLayoutBinding1 {};
    setLayoutBinding1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setLayoutBinding1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBinding1.binding = 1;
    setLayoutBinding1.descriptorCount = 1;

    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {setLayoutBinding0, setLayoutBinding1};

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    VK_CHECK(vkCreateDescriptorSetLayout(vulkanDevice.logicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &guiDescriptorSetLayout));
    log("[+] Created Gui Descriptor Set Layout");

    // Descriptor Set
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = guiDescriptorPool;
    descriptorSetAllocateInfo.pSetLayouts = &guiDescriptorSetLayout;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    VK_CHECK(vkAllocateDescriptorSets(vulkanDevice.logicalDevice, &descriptorSetAllocateInfo, &guiDescriptorSet));
    log("[+] Created Gui Descriptor Set ");

    VkDescriptorImageInfo sceneDescriptorImageInfo {};
    sceneDescriptorImageInfo.sampler = sampler;
    sceneDescriptorImageInfo.imageView = fontView;
    sceneDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo fontDescriptorImageInfo {};
    fontDescriptorImageInfo.sampler = sampler;
    fontDescriptorImageInfo.imageView = fontView;
    fontDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;


}
