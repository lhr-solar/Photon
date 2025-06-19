// ----------------------------------------------------------------------------
// GUI
// ----------------------------------------------------------------------------
//
#include "VulkanglTFModel.h"
#include "imgui_internal.h"
#include "vulkanexamplebase.h"
#include "windows.hpp"
#include <csignal>
#include <imgui.h>
#include <implot.h>
#include <implot3d.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include "backend.hpp"
#include <thread>
#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan_core.h>
#include "ui_vert_spv.hpp"
#include "ui_frag_spv.hpp"
#include "Inter_ttf.hpp"

// Options and values to display/toggle from the UI
struct UISettings {
  bool displayModels = false;
  bool displayLogos = false;
  bool displayBackground = false;
  bool displayCustomModel = true; // NEW
  bool animateLight = false;
  float lightSpeed = 0.25f;
  std::array<float, 50> frameTimes{};
  float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
  float lightTimer = 0.0f;

  glm::vec3 modelPosition = glm::vec3(0.0f);
  glm::vec3 modelRotation = glm::vec3(0.0f);
  glm::vec3 modelScale3D = glm::vec3(1.0f);
  float modelScale = 1.0f;
  glm::vec4 effectColor = glm::vec4(1.0f);
  int effectType = 0;
} uiSettings;

class GUI {
private:
  // Vulkan resources for rendering the UI
  VkSampler sampler;
  vks::Buffer vertexBuffer;
  vks::Buffer indexBuffer;
  int32_t vertexCount = 0;
  int32_t indexCount = 0;

  VkDeviceMemory fontMemory = VK_NULL_HANDLE;
  VkImage fontImage = VK_NULL_HANDLE;
  VkImageView fontView = VK_NULL_HANDLE;

  VkPipelineCache pipelineCache;
  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;

  VkDescriptorPool descriptorPool;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorSet descriptorSet;

  vks::VulkanDevice *device;
  VkPhysicalDeviceDriverProperties driverProperties = {};

  VulkanExampleBase *example;

  ImGuiStyle PhotonStyle;
  ImPlotStyle PhotonStyle_implot;
  ImPlot3DStyle PhotonStyle_implot3D;

  int selectedStyle = 0;

  // gui configuratio
  Windows configuration;
  Windows visualization;
  Windows demo;
  std::vector<Windows> tabs;
  int config_idx = 0;
  int vis_idx = 1;

  std::thread backend_thread;

public:
  // UI params are set via push constants
  ImVec2 modelWindowPos = ImVec2(0, 0);
  ImVec2 modelWindowSize = ImVec2(0, 0);

  struct PushConstBlock {
    glm::vec2 scale;
    glm::vec2 translate;
  } pushConstBlock;

  GUI(VulkanExampleBase *example) : example(example) {
    device = example->vulkanDevice;
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();

    backend_thread = std::thread(backend, 0, nullptr);

    // SRS - Set ImGui font and style scale factors to handle retina and other
    // HiDPI displays
    ImGuiIO &io = ImGui::GetIO();
    // grab font from file
    //io.Fonts->AddFontFromFileTTF("./fonts/Inter.ttf", 16.0f );
    io.Fonts->AddFontFromMemoryTTF((void*)Inter_ttf, Inter_ttf_size, 14.0f);
    io.FontGlobalScale = example->ui.scale;
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(example->ui.scale);
  };

  ~GUI() {
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    ImPlot3D::DestroyContext();
    // Release all Vulkan resources required for rendering imGui
    vertexBuffer.destroy();
    indexBuffer.destroy();
    vkDestroyImage(device->logicalDevice, fontImage, nullptr);
    vkDestroyImageView(device->logicalDevice, fontView, nullptr);
    vkFreeMemory(device->logicalDevice, fontMemory, nullptr);
    vkDestroySampler(device->logicalDevice, sampler, nullptr);
    vkDestroyPipelineCache(device->logicalDevice, pipelineCache, nullptr);
    vkDestroyPipeline(device->logicalDevice, pipeline, nullptr);
    vkDestroyPipelineLayout(device->logicalDevice, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayout,
                                 nullptr);

    if (backend_thread.joinable())
        backend_thread.join();
  }

  // Initialize styles, keys, etc.
  void init(float width, float height) {
    // Color scheme
    PhotonStyle = ImGui::GetStyle();
    ImVec4 *colors = PhotonStyle.Colors;

    // Background and primary colors
    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.7f); // Transparent soft black for background
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Full transparency for child elements
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.1f, 0.1f, 0.1f, 0.9f); // Dark gray popup with slight opacity

    // Borders and separators
    colors[ImGuiCol_Border] =
        ImVec4(0.2f, 0.2f, 0.2f, 1.0f); // Dark gray borders
    colors[ImGuiCol_Separator] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    // Text colors
    colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // Soft white text
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Dimmed gray for disabled text

    // Headers and title
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f); // Dark headers
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.25f, 0.25f, 0.25f, 1.0f); // Lighter on hover
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.3f, 0.3f, 0.3f, 1.0f); // Slightly lighter when active

    colors[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.7f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Sliders, checks, etc.
    colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    colors[ImGuiCol_CheckMark] =
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Pure white checkmark

    // Frame backgrounds
    colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);

    // Resize grips
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.2f, 0.2f, 0.2f, 0.2f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4f, 0.4f, 0.4f, 0.7f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2f, 0.2f, 0.2f, 0.7f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Misc
    colors[ImGuiCol_PlotLines] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

    colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Transparency handling
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Adjustments for sleekness
    PhotonStyle.WindowRounding = 6.0f;
    PhotonStyle.FrameRounding = 5.0f;
    PhotonStyle.ScrollbarRounding = 8.0f;
    PhotonStyle.GrabRounding = 5.0f;
    PhotonStyle.TabRounding = 5.0f;

    PhotonStyle.WindowPadding = ImVec2(12.0f, 12.0f);
    PhotonStyle.FramePadding = ImVec2(10.0f, 6.0f);
    PhotonStyle.ItemSpacing = ImVec2(12.0f, 8.0f);

    setStyle(0);

    // Dimensions
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.ConfigDockingNoSplit =
        false; // Disable splitting (what's the point then?)
    io.ConfigDockingAlwaysTabBar = false; // Enable for only tab bars (ew)

    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#if defined(_WIN32)
    // If we directly work with os specific key codes, we need to map special
    // key types like tab
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Space] = VK_SPACE;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
#endif

    // Window initialization
    visualization.parent_tab = "Visualization Window";
    configuration.parent_tab = "Configuration Window";
    demo.parent_tab = "Demo Window";

    tabs = {visualization, configuration, demo};

    tabs.at(config_idx).windows = {"CAN DBC", "Terminal",
                                   "etc"}; // default windows
    tabs.at(vis_idx).windows = {"erm1", "erm2", "erm3"};
    tabs.at(2).windows = {"t1", "t2", "t3"};
  }

  void setStyle(uint32_t index) {
    switch (index) {
    case 0: {
      ImGuiStyle &style = ImGui::GetStyle();
      style = PhotonStyle;
      break;
    }
    case 1:
      ImGui::StyleColorsClassic();
      break;
    case 2:
      ImGui::StyleColorsDark();
      break;
    case 3:
      ImGui::StyleColorsLight();
      break;
    }
  }

  // Initialize all Vulkan resources used by the ui
  void initResources(VkRenderPass renderPass, VkQueue copyQueue,
                     const std::string &shadersPath) {
    ImGuiIO &io = ImGui::GetIO();

    // Create font texture
    unsigned char *fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);

    // SRS - Get Vulkan device driver information if available, use later for
    // display
    if (device->extensionSupported(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
      VkPhysicalDeviceProperties2 deviceProperties2 = {};
      deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      deviceProperties2.pNext = &driverProperties;
      driverProperties.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
      vkGetPhysicalDeviceProperties2(device->physicalDevice,
                                     &deviceProperties2);
    }

    // Create target image for copy
    VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK_RESULT(
        vkCreateImage(device->logicalDevice, &imageInfo, nullptr, &fontImage));
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->logicalDevice, fontImage, &memReqs);
    VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo,
                                     nullptr, &fontMemory));
    VK_CHECK_RESULT(
        vkBindImageMemory(device->logicalDevice, fontImage, fontMemory, 0));

    // Image view
    VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
    viewInfo.image = fontImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr,
                                      &fontView));

    // Staging buffers for font data upload
    vks::Buffer stagingBuffer;

    VK_CHECK_RESULT(
        device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &stagingBuffer, uploadSize));

    stagingBuffer.map();
    memcpy(stagingBuffer.mapped, fontData, uploadSize);
    stagingBuffer.unmap();

    // Copy buffer data to font image
    VkCommandBuffer copyCmd =
        device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // Prepare for transfer
    vks::tools::setImageLayout(
        copyCmd, fontImage, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Copy
    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = texWidth;
    bufferCopyRegion.imageExtent.height = texHeight;
    bufferCopyRegion.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, fontImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &bufferCopyRegion);

    // Prepare for shader read
    vks::tools::setImageLayout(copyCmd, fontImage, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    device->flushCommandBuffer(copyCmd, copyQueue, true);

    stagingBuffer.destroy();

    // Font texture Sampler
    VkSamplerCreateInfo samplerInfo = vks::initializers::samplerCreateInfo();
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo,
                                    nullptr, &sampler));

    // Descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vks::initializers::descriptorPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)};
    VkDescriptorPoolCreateInfo descriptorPoolInfo =
        vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
    VK_CHECK_RESULT(vkCreateDescriptorPool(
        device->logicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));

    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT, 0),
    };
    VkDescriptorSetLayoutCreateInfo descriptorLayout =
        vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice,
                                                &descriptorLayout, nullptr,
                                                &descriptorSetLayout));

    // Descriptor set
    VkDescriptorSetAllocateInfo allocInfo =
        vks::initializers::descriptorSetAllocateInfo(descriptorPool,
                                                     &descriptorSetLayout, 1);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &allocInfo,
                                             &descriptorSet));
    VkDescriptorImageInfo fontDescriptor =
        vks::initializers::descriptorImageInfo(
            sampler, fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        vks::initializers::writeDescriptorSet(
            descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0,
            &fontDescriptor)};
    vkUpdateDescriptorSets(device->logicalDevice,
                           static_cast<uint32_t>(writeDescriptorSets.size()),
                           writeDescriptorSets.data(), 0, nullptr);

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VK_CHECK_RESULT(vkCreatePipelineCache(device->logicalDevice,
                                          &pipelineCacheCreateInfo, nullptr,
                                          &pipelineCache));

    // Pipeline layout
    // Push constants for UI rendering parameters
    VkPushConstantRange pushConstantRange =
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT,
                                             sizeof(PushConstBlock), 0);
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
        vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device->logicalDevice,
                                           &pipelineLayoutCreateInfo, nullptr,
                                           &pipelineLayout));

    // Setup graphics pipeline for UI rendering
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
        vks::initializers::pipelineInputAssemblyStateCreateInfo(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

    VkPipelineRasterizationStateCreateInfo rasterizationState =
        vks::initializers::pipelineRasterizationStateCreateInfo(
            VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
            VK_FRONT_FACE_COUNTER_CLOCKWISE);

    // Enable blending
    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.blendEnable = VK_TRUE;
    blendAttachmentState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachmentState.srcAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlendState =
        vks::initializers::pipelineColorBlendStateCreateInfo(
            1, &blendAttachmentState);

    VkPipelineDepthStencilStateCreateInfo depthStencilState =
        vks::initializers::pipelineDepthStencilStateCreateInfo(
            VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

    VkPipelineViewportStateCreateInfo viewportState =
        vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

    VkPipelineMultisampleStateCreateInfo multisampleState =
        vks::initializers::pipelineMultisampleStateCreateInfo(
            VK_SAMPLE_COUNT_1_BIT);

    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState =
        vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkGraphicsPipelineCreateInfo pipelineCreateInfo =
        vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pRasterizationState = &rasterizationState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    // Vertex bindings an attributes based on ImGui vertex definition
    std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
        vks::initializers::vertexInputBindingDescription(
            0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX),
    };
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        vks::initializers::vertexInputAttributeDescription(
            0, 0, VK_FORMAT_R32G32_SFLOAT,
            offsetof(ImDrawVert, pos)), // Location 0: Position
        vks::initializers::vertexInputAttributeDescription(
            0, 1, VK_FORMAT_R32G32_SFLOAT,
            offsetof(ImDrawVert, uv)), // Location 1: UV
        vks::initializers::vertexInputAttributeDescription(
            0, 2, VK_FORMAT_R8G8B8A8_UNORM,
            offsetof(ImDrawVert, col)), // Location 0: Color
    };
    VkPipelineVertexInputStateCreateInfo vertexInputState =
        vks::initializers::pipelineVertexInputStateCreateInfo();
    vertexInputState.vertexBindingDescriptionCount =
        static_cast<uint32_t>(vertexInputBindings.size());
    vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
    vertexInputState.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(vertexInputAttributes.size());
    vertexInputState.pVertexAttributeDescriptions =
        vertexInputAttributes.data();

    pipelineCreateInfo.pVertexInputState = &vertexInputState;

    /*
    shaderStages[0] = example->loadShader(shadersPath + "imgui/ui.vert.spv",
                                          VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = example->loadShader(shadersPath + "imgui/ui.frag.spv",
                                          VK_SHADER_STAGE_FRAGMENT_BIT);
                                          */
    shaderStages[0] = example->loadShader(ui_vert_spv, ui_vert_spv_size, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = example->loadShader(ui_frag_spv, ui_frag_spv_size, VK_SHADER_STAGE_FRAGMENT_BIT);

    VK_CHECK_RESULT(
        vkCreateGraphicsPipelines(device->logicalDevice, pipelineCache, 1,
                                  &pipelineCreateInfo, nullptr, &pipeline));

      }

  // creates the main dock, at this point, only modify for aesthetics
  void createMainSpace(std::vector<Windows> tabs) {
    ImGuiID id = ImGui::GetID("MainWindowGroup");
    ImGuiDockNodeFlags mainDockNodeFlags =
        ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDocking;

    static bool firstLoop = true;
    if (firstLoop) {
      ImGui::DockBuilderRemoveNode(id);
      //      ImGui::DockBuilderAddNode(id, mainDockNodeFlags); // Pass
      //      DockNodeFlags
      ImGui::DockBuilderAddNode(id); // Pass DockNodeFlags
      for (int i = 0; i < tabs.size(); i++) {
        ImGui::DockBuilderDockWindow(tabs.at(i).parent_tab.c_str(), id);
      }

      ImGui::DockBuilderFinish(id);
      firstLoop = false;
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodePos(id, viewport->Pos);
    ImGui::DockBuilderSetNodeSize(id, viewport->Size);
  }

  // creates the docking space for the passed in tab,
  // also reserves locations for windows
  void createTabDock(Windows &tab) {
    // create docks within tabs
    ImGui::Begin(tab.parent_tab.c_str());
    ImGuiID dock_id = ImGui::GetID((tab.parent_tab + "Dock").c_str());
    ImGui::DockSpace(dock_id);
    if (!tab.initialized) {
      tab.initialized = true;
      ImGui::DockBuilderRemoveNode(dock_id);
      ImGui::DockBuilderAddNode(dock_id); // flags here

      // here is where you programatically add docks
      // at the moment, just generates tabs for every window
      // golden ratio it ? lol nah
      for (int i = 0; i < tab.windows.size(); i++) {
        ImGui::DockBuilderDockWindow(tab.windows.at(i).c_str(), dock_id);
      }

      ImGui::DockBuilderFinish(dock_id);
    }
    ImGui::End();
  }

  void createTSPlot(std::string windowName) {
    static float xs1[1001], ys1[1001];
    for (int i = 0; i < 1001; ++i) {
      xs1[i] = i * 0.001f;
      ys1[i] = 0.5f + 0.5f * tanf(50 * (xs1[i] + (float)ImGui::GetTime() / 10));
      //ys1[i] = 0.5f + i / 0.5f;
    }

    ImGui::Begin(windowName.c_str());
    ImGui::Text("Window Name: %s", windowName.c_str());
    if (ImPlot::BeginPlot("Sync")) {
      ImPlot::SetupAxes("x", "y");
      ImPlot::PlotLine("f(x)", xs1, ys1, 500);
      ImPlot::PlotLine("f(x)", xs1, ys1, 500);
      ImPlot::EndPlot();
    }
    ImGui::End();
  }

  void sourceConfigWindow(){

      // -- state --
      static int input_flag = 0;
      static int close_flag = 0;

      // -- input buffers --
      static char serialBuf[64] = "";
      static char baudBuf[16]   = "";
      static char ipBuf[64]     = "";
      static char portBuf[8]    = "";

      // -- hints --
      static std::string serialHint = "e.g. /dev/ttyUSB0";
      static std::string baudHint   = "e.g. 115200";
      static std::string ipHint     = "e.g. 192.168.1.2";
      static std::string portHint   = "e.g. 8080";

      
      ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Once);
      ImGui::Begin("Data Source");

      const char* protocol_list[] = {"Serial", "TCP"};
      static int protocol_idx = 0;
      ImGui::Combo("", &protocol_idx, protocol_list, ((int)sizeof(protocol_list) / sizeof(*(protocol_list))));
      ImGui::SameLine();
      if(ImGui::Button("Close Connection"))
        close_flag = 1;

      if(protocol_idx == 0){
        ImGui::InputTextWithHint("  ", serialHint.c_str(), serialBuf, sizeof(serialBuf));
        ImGui::InputTextWithHint(" ", baudHint.c_str(), baudBuf, sizeof(baudBuf), ImGuiInputTextFlags_CharsDecimal);
      }
      if(protocol_idx == 1){
        ImGui::InputTextWithHint("  ", ipHint.c_str(), ipBuf, sizeof(ipBuf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank);
        ImGui::InputTextWithHint(" ", portHint.c_str(), portBuf, sizeof(baudBuf), ImGuiInputTextFlags_CharsDecimal);
       }
      ImGui::SameLine();
      if(ImGui::Button("Connect"))
          input_flag = 1;

      if(close_flag == 1){
          kill_data_source();
          close_flag = 0;
          serialBuf[0] = baudBuf[0] = ipBuf[0] = portBuf[0] = '\0';
      }

      if(input_flag == 1){
          input_flag = 0;
          if(protocol_idx == 0){
            std::string portStr(serialBuf);
            std::string baudStr(baudBuf);
            forward_serial_source(portStr, baudStr);
            serialHint = (!portStr.empty()) ? portStr : "e.g. /dev/ttyUSB0";
            baudHint   = (!baudStr.empty()) ? baudStr : "e.g. 115200";
          }
          if(protocol_idx == 1){
            std::string ipStr(ipBuf);
            std::string prtStr(portBuf);
            forward_tcp_source(ipStr, prtStr);
            ipHint   = (!ipStr.empty())  ? ipStr  : "e.g. 192.168.1.2";
            portHint = (!prtStr.empty()) ? prtStr : "e.g. 8080";
          }

          serialBuf[0] = baudBuf[0] = ipBuf[0] = portBuf[0] = '\0';
      }
      ImGui::End();
  }

  void AnimatedTablePlot() {
    static ImGuiTableFlags flags =
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable;
    static int offset = 0;

    // Animate the offset
    offset = (offset + 1) % 100;

    // Create a floating window and bring it to the front
    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Power Generation", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    // Display the table with animated data
    if (ImGui::BeginTable("##table", 3, flags, ImVec2(-1, 0))) {
      ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthFixed,
                              75.0f);
      ImGui::TableSetupColumn("Voltage", ImGuiTableColumnFlags_WidthFixed,
                              75.0f);
      ImGui::TableSetupColumn("Signal Info");
      ImGui::TableHeadersRow();
      ImPlot::PushColormap(ImPlotColormap_Cool);

      for (int row = 0; row < 10; row++) {
        ImGui::TableNextRow();
        static float data[100];
        for (int i = 0; i < 100; ++i)
          data[i] = (i + row) % 10; // Simple data for demonstration

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("EMG %d", row);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f V", data[offset]);

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("Signal λ %d", offset);
      }

      ImPlot::PopColormap();
      ImGui::EndTable();
    }

    ImGui::End();
  }

  void SurfacePlot() {
    constexpr int N = 20;
    static float xs[N * N], ys[N * N], zs[N * N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    // Define the range for X and Y
    constexpr float min_val = -1.0f;
    constexpr float max_val = 1.0f;
    constexpr float step = (max_val - min_val) / (N - 1);

    for (int i = 0; i < N; i++) {
      for (int j = 0; j < N; j++) {
        int idx = i * N + j;
        xs[idx] = min_val + j * step;
        ys[idx] = min_val + i * step;

        float r2 = xs[idx] * xs[idx] + ys[idx] * ys[idx];
        zs[idx] = ImSin(r2 - t) * ImCos(3 * xs[idx]) * ImSin(3 * ys[idx]);
      }
    }

    // Create a floating window
    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f); // Slight transparency
    ImGui::Begin("Distribution", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    // Begin the plot
    ImPlot3D::PushColormap("Jet");
    if (ImPlot3D::BeginPlot("Ts4 Q3",
                            ImVec2(-1, -1), ImPlot3DFlags_NoClip)) {
      ImPlot3D::SetupAxesLimits(-1, 1, -1, 1, -1.5, 1.5);
      ImPlot3D::PushStyleVar(ImPlot3DStyleVar_FillAlpha, 0.8f);
      ImPlot3D::PlotSurface("Wave Surface", xs, ys, zs, N, N);
      ImPlot3D::PopStyleVar();
      ImPlot3D::EndPlot();
    }
    ImPlot3D::PopColormap();
    ImGui::End();
  }

  void CAN_TABLE(){
          static ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter |
                                   ImGuiTableFlags_BordersV |
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_ScrollY;

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Once);

    if (ImGui::Begin("CAN Data")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
      if (ImGui::BeginTable("cantable", 3, flags)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Len", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Decoded");
        ImGui::TableHeadersRow();

        const CanStore &store = get_can_store();
        for (uint32_t id = 0; id < CanStore::MAX_IDS; ++id) {
          CanFrame frame;
          if (store.read(id, frame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("0x%03X", id);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", frame.len);

            ImGui::TableSetColumnIndex(2);
            std::string decoded;
            if (backend_decode(id, frame, decoded))
              ImGui::TextUnformatted(decoded.c_str());
            else {
              char buf[3 * 8 + 1] = {0};
              for (uint8_t i = 0; i < frame.len; ++i)
                sprintf(buf + i * 3, "%02X ", frame.data[i]);
              ImGui::TextUnformatted(buf);
            }
          }
        }
        ImGui::EndTable();
      }
    }
    ImGui::End();
  }

  void Modelwindow(){
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Once);
    ImGui::Begin("Model Window");
    modelWindowPos = ImGui::GetWindowPos();
    modelWindowSize = ImGui::GetWindowSize();
    ImGui::SliderFloat3("Position", glm::value_ptr(uiSettings.modelPosition), -5.0f, 5.0f);

    ImGui::SliderFloat3("Rotation", glm::value_ptr(uiSettings.modelRotation), -180.0f, 180.0f);
    ImGui::SliderFloat3("Scale XYZ", glm::value_ptr(uiSettings.modelScale3D), 0.1f, 5.0f);

    ImGui::SliderFloat("Scale", &uiSettings.modelScale, 0.1f, 5.0f);
    ImGui::ColorEdit4("Effect", glm::value_ptr(uiSettings.effectColor));

    const char * effects[] = {"None", "Invert", "Grayscale"};
    ImGui::Combo("Effect Type", &uiSettings.effectType, effects, IM_ARRAYSIZE(effects));

    ImGui::End();

  }



  void setupDocking(){
      createMainSpace(tabs);
      for(auto&tab : tabs){
          createTabDock(tab);
      }
  }

  void drawTabPlots(){
      for(auto &tab : tabs){
          for(auto & window : tab.windows){
              createTSPlot(window.c_str());
          }
      }
  }

void dbcConfigWindow(){
      static char pathBuf[256] = "";
      ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Once);
      ImGui::Begin("DBC Config");

      ImGui::InputText("File", pathBuf, sizeof(pathBuf));
      ImGui::SameLine();
      if(ImGui::Button("Load")){
          std::string p(pathBuf);
          forward_dbc_load(p);
          pathBuf[0] = '\0';
      }

      ImGui::Separator();
      ImGui::Text("Embedded Files:");
      auto builtins = list_builtin_dbcs();
      for(const auto &b : builtins){
          bool enabled = b.second;
          if(ImGui::Checkbox(b.first.c_str(), &enabled)){
              if(enabled)
                  forward_builtin_dbc_load(b.first);
              else
                  forward_builtin_dbc_unload(b.first);
          }
      }

      ImGui::Separator();
      ImGui::Text("Loaded Files:");
      auto files = get_loaded_dbcs();
      for(size_t i = 0; i < files.size(); ++i){
          ImGui::TextUnformatted(files[i].c_str());
          ImGui::SameLine();
          std::string btn = "Unload##" + std::to_string(i);
          if(ImGui::Button(btn.c_str())){
              forward_dbc_unload(files[i]);
          }
      }

      ImGui::End();
  }

  void drawDemoWindows(){
      CAN_TABLE();
      SurfacePlot();
      //Modelwindow();
      createTSPlot("test");
      sourceConfigWindow();
      dbcConfigWindow();
  }

  // Starts a new imGui frame and sets up windows and ui elements
  void newFrame(VulkanExampleBase *example, bool updateFrameGraph) {
    // move all this to init, make declarations in public class
    ImGui::NewFrame();
    // create main space, needs to know the number of tabs, this is done, maybe
    // create a new type, "tab"
    //setupDocking();
    //drawTabPlots();
    drawDemoWindows();
    /*
    createMainSpace(tabs);

    // create the docking space for each tab, needs to know the # of windows
    // and it's properties, make placement decisions
    for (int i = 0; i < tabs.size(); i++) {
      createTabDock(tabs.at(i));
    }

    // assigns the different windows to plots
    for (int i = 0; i < tabs.at(0).windows.size(); i++) {
      createTSPlot(tabs.at(0).windows.at(i).c_str());
    }

    for (int i = 0; i < tabs.at(1).windows.size(); i++) {
      createTSPlot(tabs.at(1).windows.at(i).c_str());
    }

    // Create the Main Space, need to know how many tabs you want
    // Create Docking Area for each Tab, need to know how many windows it wants,
    // and the dimensions of the windows Create Window Plots, need to know the
    // type of data being placed in the windows Update Window Plots, update
    // windows based on buffer data

    CAN_TABLE();

    SurfacePlot();

    Modelwindow();

    // TODO
    //AnimatedTablePlot();
    //TimeSeriesPlot();
    //MagnitudePlot();
    //PhasePlot();
    //PowerPlot();
    //SpherePointCloud();
    //HeatmapPlot();
   // BlackHolePointCloud();
    */

    ImGui::Render();
  }

void HeatmapPlot() {
    static float values1[7][7] = {
        {0.8f, 2.4f, 2.5f, 3.9f, 0.0f, 4.0f, 0.0f},
        {2.4f, 0.0f, 4.0f, 1.0f, 2.7f, 0.0f, 0.0f},
        {1.1f, 2.4f, 0.8f, 4.3f, 1.9f, 4.4f, 0.0f},
        {0.6f, 0.0f, 0.3f, 0.0f, 3.1f, 0.0f, 0.0f},
        {0.7f, 1.7f, 0.6f, 2.6f, 2.2f, 6.2f, 0.0f},
        {1.3f, 1.2f, 0.0f, 0.0f, 0.0f, 3.2f, 5.1f},
        {0.1f, 2.0f, 0.0f, 1.4f, 0.0f, 1.9f, 6.3f}
    };
    static float scale_min = 0.0f;
    static float scale_max = 6.3f;
    static const char* xlabels[] = {"C1", "C2", "C3", "C4", "C5", "C6", "C7"};
    static const char* ylabels[] = {"R1", "R2", "R3", "R4", "R5", "R6", "R7"};

    static const int size = 80;
    static double values2[size * size];

    // Regenerate visual noise pattern every frame
    srand((unsigned int)(ImGui::GetTime() * 1000000));
    for (int i = 0; i < size * size; ++i)
        values2[i] = (double)rand() / RAND_MAX;

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Heatmaps", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    ImPlot::PushColormap(ImPlotColormap_Cool);

    // 7x7 labeled matrix heatmap
    if (ImPlot::BeginPlot("##Heatmap1", ImVec2(225, 225), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr,
            ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks,
            ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks);
        ImPlot::SetupAxisTicks(ImAxis_X1, 0 + 1.0 / 14.0, 1 - 1.0 / 14.0, 7, xlabels);
        ImPlot::SetupAxisTicks(ImAxis_Y1, 1 - 1.0 / 14.0, 0 + 1.0 / 14.0, 7, ylabels);
        ImPlot::PlotHeatmap("heat", values1[0], 7, 7, scale_min, scale_max, "%g", ImPlotPoint(0, 0), ImPlotPoint(1, 1));
        ImPlot::EndPlot();
    }

    ImGui::SameLine();

    // Pure visual noise heatmap with two overlaid layers
    if (ImPlot::BeginPlot("##Heatmap2", ImVec2(225, 225))) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxesLimits(-1, 1, -1, 1);
        ImPlot::PlotHeatmap("heat1", values2, size, size, 0, 1, nullptr);
        ImPlot::PlotHeatmap("heat2", values2, size, size, 0, 1, nullptr, ImPlotPoint(-1, -1), ImPlotPoint(0, 0));
        ImPlot::EndPlot();
    }

    ImPlot::PopColormap();
    ImGui::End();
}

  void BlackHolePointCloud() {
    constexpr int SPHERE_POINTS = 500; // Dense center
    constexpr int RING_POINTS = 500;   // Accretion disk
    constexpr int N = SPHERE_POINTS + RING_POINTS;
    static float xs[N], ys[N], zs[N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    // Generate the central sphere (Event Horizon)
    for (int i = 0; i < SPHERE_POINTS; ++i) {
        float u = (float)rand() / RAND_MAX;
        float v = (float)rand() / RAND_MAX;
        
        float theta = 2.0f * IM_PI * u;  
        float phi = acosf(2.0f * v - 1.0f);

        float r = 0.3f + 0.02f * sinf(5.0f * t + theta); // Small pulsation effect
        xs[i] = r * sinf(phi) * cosf(theta);
        ys[i] = r * sinf(phi) * sinf(theta);
        zs[i] = r * cosf(phi);
    }

    // Generate the rotating rings (Accretion Disk)
    for (int i = 0; i < RING_POINTS; ++i) {
        float angle = 2.0f * IM_PI * (i % 100) / 100.0f + 0.5f * t; // Rotation effect
        float radius = 0.5f + 0.2f * ((i / 100) % 10) / 10.0f;      // Multi-layered rings

        // Warping effect to simulate gravitational distortion
        float distortion = 0.05f * sinf(3.0f * angle + t);
        xs[SPHERE_POINTS + i] = (radius + distortion) * cosf(angle);
        ys[SPHERE_POINTS + i] = (radius + distortion) * sinf(angle);
        zs[SPHERE_POINTS + i] = 0.05f * cosf(2.0f * t + angle); // Slight oscillation
    }

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Black Hole Point Cloud", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImPlot3D::BeginPlot("Black Hole", ImVec2(-1, -1), ImPlot3DFlags_NoClip)) {
        ImPlot3D::SetupAxesLimits(-1.2, 1.2, -1.2, 1.2, -1.2, 1.2);

        // Reduce point size for a smooth look
        ImPlot3D::PushStyleVar(ImPlot3DStyleVar_MarkerSize, 1.5f);

        ImPlot3D::PlotScatter("Event Horizon", xs, ys, zs, SPHERE_POINTS);
        ImPlot3D::PlotScatter("Accretion Disk", &xs[SPHERE_POINTS], &ys[SPHERE_POINTS], &zs[SPHERE_POINTS], RING_POINTS);

        ImPlot3D::PopStyleVar();
        ImPlot3D::EndPlot();
    }

    ImGui::End();
}


  void SpherePointCloud() {
    constexpr int N = 1000; // Increase number of points for a denser cloud
    static float xs[N], ys[N], zs[N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    // Use a randomized distribution for a more natural point spread
    for (int i = 0; i < N; ++i) {
        float u = (float)rand() / RAND_MAX; // Randomized spherical distribution
        float v = (float)rand() / RAND_MAX;
        
        float theta = 2.0f * IM_PI * u;  // Random longitude
        float phi = acosf(2.0f * v - 1.0f); // Random latitude (avoiding clustering at poles)

        float r = 1.0f + 0.05f * sinf(3.0f * t + theta); // Subtle pulsation effect
        xs[i] = r * sinf(phi) * cosf(theta);
        ys[i] = r * sinf(phi) * sinf(theta);
        zs[i] = r * cosf(phi);
    }

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Point Distribution", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImPlot3D::BeginPlot("x3 14t", ImVec2(-1, -1), ImPlot3DFlags_NoClip)) {
        ImPlot3D::SetupAxesLimits(-1.2, 1.2, -1.2, 1.2, -1.2, 1.2);
        ImPlot3D::PushStyleVar(ImPlot3DStyleVar_MarkerSize, 0.8f);
        ImPlot3D::PlotScatter("Points", xs, ys, zs, N);
        ImPlot3D::EndPlot();
    }

    ImGui::End();
}


  void PowerPlot() {
    constexpr int N = 500;
    static float freq[N], power[N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    for (int i = 0; i < N; ++i) {
        freq[i] = i + 1;
        power[i] = -50.0f + 20.0f * expf(-0.005f * freq[i]) + 5.0f * sinf(0.05f * freq[i] + 0.3f * t);
    }

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Power Spectrum", nullptr);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImPlot::BeginPlot("Power", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Frequency [Hz]", "Power [dB]");
        ImPlot::SetupAxesLimits(1, 500, -100, 0, ImPlotCond_Always);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::PlotLine("Power Density", freq, power, N);
        ImPlot::EndPlot();
    }

    ImGui::End();
}


  void PhasePlot() {
    constexpr int N = 500;
    static float freq[N], phase[N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    for (int i = 0; i < N; ++i) {
        freq[i] = i + 1;
        phase[i] = 180.0f * sinf(0.01f * freq[i] - 0.2f * t);
    }

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Phase Spectrum", nullptr);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImPlot::BeginPlot("Phase", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Frequency [Hz]", "Phase Angle [deg]");
        ImPlot::SetupAxesLimits(1, 500, -180, 180, ImPlotCond_Always);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::PlotLine("Phase Shift", freq, phase, N);
        ImPlot::EndPlot();
    }

    ImGui::End();
}


  void MagnitudePlot() {
    constexpr int N = 500;
    static float freq[N], magnitude[N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    for (int i = 0; i < N; ++i) {
        freq[i] = i + 1;
        magnitude[i] = 10.0f / (1.0f + 0.02f * freq[i] * freq[i]) + 0.2f * sinf(0.1f * freq[i] + t);
    }

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Magnitude Spectrum", nullptr);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImPlot::BeginPlot("Magnitude", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");
        ImPlot::SetupAxesLimits(1, 500, -5, 12, ImPlotCond_Always);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::PlotLine("Magnitude", freq, magnitude, N);
        ImPlot::EndPlot();
    }

    ImGui::End();
}


  void TimeSeriesPlot() {
    constexpr int N = 1000;
    static float xs[N], ys[N];
    static float t = 0.0f;
    t += ImGui::GetIO().DeltaTime;

    for (int i = 0; i < N; ++i) {
        xs[i] = i * 0.001f;
        ys[i] = sinf(15.0f * (xs[i] + 0.5f * t)) * cosf(10.0f * xs[i] - 0.3f * t) * (1.0f + 0.1f * sinf(5.0f * t));
    }

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Time Series Plot", nullptr);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImPlot::BeginPlot("Signal", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time [s]", "Amplitude");
        ImPlot::SetupAxesLimits(0, 1, -1.5, 1.5);
        ImPlot::PlotLine("Waveform", xs, ys, N);
        ImPlot::EndPlot();
    }

    ImGui::End();
}

  // Update vertex and index buffer containing the imGui elements when required
  void updateBuffers() {
    ImDrawData *imDrawData = ImGui::GetDrawData();

    // Note: Alignment is done inside buffer creation
    VkDeviceSize vertexBufferSize =
        imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize =
        imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

    if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
      return;
    }

    // Update buffers only if vertex or index count has been changed compared to
    // current buffer size

    // Vertex buffer
    if ((vertexBuffer.buffer == VK_NULL_HANDLE) ||
        (vertexCount != imDrawData->TotalVtxCount)) {
      vertexBuffer.unmap();
      vertexBuffer.destroy();
      VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                           &vertexBuffer, vertexBufferSize));
      vertexCount = imDrawData->TotalVtxCount;
      vertexBuffer.map();
    }

    // Index buffer
    if ((indexBuffer.buffer == VK_NULL_HANDLE) ||
        (indexCount < imDrawData->TotalIdxCount)) {
      indexBuffer.unmap();
      indexBuffer.destroy();
      VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                           &indexBuffer, indexBufferSize));
      indexCount = imDrawData->TotalIdxCount;
      indexBuffer.map();
    }

    // Upload data
    ImDrawVert *vtxDst = (ImDrawVert *)vertexBuffer.mapped;
    ImDrawIdx *idxDst = (ImDrawIdx *)indexBuffer.mapped;

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data,
             cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data,
             cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }

    // Flush to make writes visible to GPU
    vertexBuffer.flush();
    indexBuffer.flush();
  }

  // Draw current imGui frame into a command buffer
  void drawFrame(VkCommandBuffer commandBuffer) {
    ImGuiIO &io = ImGui::GetIO();

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport = vks::initializers::viewport(
        ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    // UI scale and translate via push constants
    pushConstBlock.scale =
        glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
    pushConstBlock.translate = glm::vec2(-1.0f);
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock),
                       &pushConstBlock);

    // Render commands
    ImDrawData *imDrawData = ImGui::GetDrawData();
    int32_t vertexOffset = 0;
    int32_t indexOffset = 0;

    if (imDrawData->CmdListsCount > 0) {

      VkDeviceSize offsets[1] = {0};
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer,
                             offsets);
      vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0,
                           VK_INDEX_TYPE_UINT16);

      for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList *cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
          const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];
          VkRect2D scissorRect;
          scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
          scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
          scissorRect.extent.width =
              (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
          scissorRect.extent.height =
              (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
          vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
          vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, indexOffset,
                           vertexOffset, 0);
          indexOffset += pcmd->ElemCount;
        }
        vertexOffset += cmd_list->VtxBuffer.Size;
      }
    }
  }

};
