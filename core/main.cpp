/*
 * Photon Core
 */

#include "VulkanTools.h"
#include "VulkanglTFModel.h"
#include "gui.hpp"
#include "vulkanexamplebase.h"
#include <imgui.h>
#include <vulkan/vulkan_core.h>
#include <glm/gtc/matrix_transform.hpp>
#include "scene_vert_spv.hpp"
#include "scene_frag_spv.hpp"

#define VK_USE_PLATFORM_WAYLAND_KHR

VkSampler sampler;

// ----------------------------------------------------------------------------
// Photon
// ----------------------------------------------------------------------------

class Photon : public VulkanExampleBase {
public:
  GUI *gui = nullptr;

  struct Models {
    vkglTF::Model models;
    vkglTF::Model logos;
    vkglTF::Model background;
    vkglTF::Model customModel; // new
    vkglTF::Model aeroShell;
  } models;

  // step 2
  vks::Buffer particleBuffer;
  struct Particle {
    glm::vec4 position;
    glm::vec4 velocity;
    float life;
  };
  std::vector<Particle> particles;

  vks::Buffer uniformBufferVS;

  struct UBOVS {
    glm::mat4 projection;
    glm::mat4 modelview;
    glm::vec4 lightPos;
  } uboVS;

  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;
  VkPipeline customPipeline; // custom
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorSet descriptorSet;

  Photon() : VulkanExampleBase() {
    title = "Photon";
    camera.type = Camera::CameraType::lookat;
    camera.setPosition(glm::vec3(0.0f, 0.0f, -4.8f));
    camera.setRotation(glm::vec3(4.5f, -380.0f, 0.0f));
    camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);

    // SRS - Enable VK_KHR_get_physical_device_properties2 to retrieve device
    // driver information for display
    enabledInstanceExtensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    // Don't use the ImGui overlay of the base framework in this sample
    settings.overlay = true;
  }

  ~Photon() {
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipeline(device, customPipeline, nullptr); // custom
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    uniformBufferVS.destroy();

    delete gui;
  }

  void buildCommandBuffers() {
    // I lied ... it happens here :)
    VkCommandBufferBeginInfo cmdBufInfo =
        vks::initializers::commandBufferBeginInfo();

    VkClearValue clearValues[2];
    // clearValues[0].color = {{0.2f, 0.2f, 0.2f, 1.0f}}; // light gray
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassBeginInfo =
        vks::initializers::renderPassBeginInfo();
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;

    gui->newFrame(this, (frameCounter == 0));
    gui->updateBuffers();
    // step 10
    // particleBuffer.flush();

    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
      // Set target frame buffer
      renderPassBeginInfo.framebuffer = frameBuffers[i];

      VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

      vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo,
                           VK_SUBPASS_CONTENTS_INLINE);

      VkViewport viewport =
          vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
      vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

      VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
      vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

      // Render scene
      vkCmdBindDescriptorSets(drawCmdBuffers[i],
                              VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                              0, 1, &descriptorSet, 0, nullptr);
      // step 13? - not causing seg fault tho
      vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

      VkDeviceSize offsets[1] = {0};
      // step 7
      /*
      vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                        customPipeline);
      vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &particleBuffer.buffer,
                             offsets);
      vkCmdDraw(drawCmdBuffers[i], static_cast<uint32_t>(particles.size()), 1,
                0, 0);
                */

      if (uiSettings.displayBackground) {
        models.background.draw(drawCmdBuffers[i]);
      }

      if (uiSettings.displayModels) {
        models.models.draw(drawCmdBuffers[i]);
      }

      if (uiSettings.displayLogos) {
        models.logos.draw(drawCmdBuffers[i]);
      }

      if (uiSettings.displayCustomModel) { // imgui to toggle visibility
        VkViewport modelViewport = vks::initializers::viewport(gui->modelWindowSize.x, gui->modelWindowSize.y, 0.0f, 1.0f);
        modelViewport.x = gui->modelWindowPos.x;
        modelViewport.y = gui->modelWindowPos.y;
        vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &modelViewport);

        VkRect2D modelScissor = vks::initializers::rect2D(
            (uint32_t)gui->modelWindowSize.x, (uint32_t)gui->modelWindowSize.y,
            (int32_t)gui->modelWindowPos.x, (int32_t)gui->modelWindowPos.y);
        vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &modelScissor);

        struct PushData {
          glm::mat4 transform;
          glm::vec4 effectColor;
          int effectType;
        } pushData;

        //pushData.transform = glm::translate(glm::mat4(1.0f), uiSettings.modelPosition) * glm::scale(glm::mat4(1.0f), glm::vec3(uiSettings.modelScale));
        glm::mat4 modelMat = glm::mat4(1.0f);//
        modelMat = glm::translate(modelMat, uiSettings.modelPosition);//
        modelMat = glm::rotate(modelMat, glm::radians(uiSettings.modelRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));//
        modelMat = glm::rotate(modelMat, glm::radians(uiSettings.modelRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));//
        modelMat = glm::rotate(modelMat, glm::radians(uiSettings.modelRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));//
        modelMat = glm::scale(modelMat, uiSettings.modelScale3D * uiSettings.modelScale);//
        pushData.transform = modelMat; //
        pushData.effectColor = uiSettings.effectColor;
        pushData.effectType = uiSettings.effectType; //
        vkCmdPushConstants(drawCmdBuffers[i], pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushData), &pushData);

        models.customModel.draw(drawCmdBuffers[i]);
        vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
        vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
      }

      // Render imGui
      if (ui.visible) {
        gui->drawFrame(drawCmdBuffers[i]);
      }

      vkCmdEndRenderPass(drawCmdBuffers[i]);

      VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
    }
  }

  void setupLayoutsAndDescriptors() {
    // descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                              2),
        vks::initializers::descriptorPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)};
    VkDescriptorPoolCreateInfo descriptorPoolInfo =
        vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr,
                                           &descriptorPool));

    // Set layout
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
    };
    VkDescriptorSetLayoutCreateInfo descriptorLayout =
        vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout,
                                                nullptr, &descriptorSetLayout));

    // Pipeline layout
        VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    //pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec4);
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(int);
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
        vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo,
                                           nullptr, &pipelineLayout));

    // Descriptor set
    VkDescriptorSetAllocateInfo allocInfo =
        vks::initializers::descriptorSetAllocateInfo(descriptorPool,
                                                     &descriptorSetLayout, 1);
    VK_CHECK_RESULT(
        vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        vks::initializers::writeDescriptorSet(descriptorSet,
                                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                              0, &uniformBufferVS.descriptor),
    };
    vkUpdateDescriptorSets(device,
                           static_cast<uint32_t>(writeDescriptorSets.size()),
                           writeDescriptorSets.data(), 0, nullptr);
  }

  void preparePipelines() {
    // Rendering
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
        vks::initializers::pipelineInputAssemblyStateCreateInfo(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState =
        vks::initializers::pipelineRasterizationStateCreateInfo(
            VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT,
            VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState =
        vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState =
        vks::initializers::pipelineColorBlendStateCreateInfo(
            1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState =
        vks::initializers::pipelineDepthStencilStateCreateInfo(
            VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState =
        vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
    VkPipelineMultisampleStateCreateInfo multisampleState =
        vks::initializers::pipelineMultisampleStateCreateInfo(
            VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState =
        vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI =
        vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
        {vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal,
         vkglTF::VertexComponent::Color});
    ;

    /*
    shaderStages[0] = loadShader(getShadersPath() + "imgui/scene.vert.spv",
                                 VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + "imgui/scene.frag.spv",
                                 VK_SHADER_STAGE_FRAGMENT_BIT);
    */
    shaderStages[0] = loadShader(scene_vert_spv, scene_vert_spv_size, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(scene_frag_spv, scene_frag_spv_size, VK_SHADER_STAGE_FRAGMENT_BIT);

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1,
                                              &pipelineCI, nullptr, &pipeline));

    // custom pipeline stuff

    // new blend stuff V
    /*
VkPipelineColorBlendAttachmentState CustomblendAttachmentState =
    vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_TRUE);
CustomblendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
CustomblendAttachmentState.dstColorBlendFactor =
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
CustomblendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
CustomblendAttachmentState.srcAlphaBlendFactor =
    VK_BLEND_FACTOR_ONE; // Maintain alpha in blending
CustomblendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
CustomblendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

VkPipelineColorBlendStateCreateInfo CustomcolorBlendState =
    vks::initializers::pipelineColorBlendStateCreateInfo(
        1, &CustomblendAttachmentState);

depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(
    VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);

    */
    // new blend stuff ^

    /*
    // step 11
    // Particle vertex binding: Each particle has a position and velocity
    std::vector<VkVertexInputBindingDescription> particleVertexBinding = {
        vks::initializers::vertexInputBindingDescription(
            0, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX)};

    // Particle vertex attributes: Position (vec3), Velocity (vec3)
    std::vector<VkVertexInputAttributeDescription> particleVertexAttributes = {
        vks::initializers::vertexInputAttributeDescription(
            0, 0, VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Particle, position)), // Location 0: Position
        vks::initializers::vertexInputAttributeDescription(
            0, 1, VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Particle, velocity)) // Location 1: Velocity
    };

    // Configure particle vertex input state
    VkPipelineVertexInputStateCreateInfo particleVertexInputState =
        vks::initializers::pipelineVertexInputStateCreateInfo();
    particleVertexInputState.vertexBindingDescriptionCount =
        static_cast<uint32_t>(particleVertexBinding.size());
    particleVertexInputState.pVertexBindingDescriptions =
        particleVertexBinding.data();
    particleVertexInputState.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(particleVertexAttributes.size());
    particleVertexInputState.pVertexAttributeDescriptions =
        particleVertexAttributes.data();

    // step 6
    // remember to change ci dots below !
    VkPipelineInputAssemblyStateCreateInfo CustominputAssemblyState =
        vks::initializers::pipelineInputAssemblyStateCreateInfo(
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, VK_FALSE);

    std::array<VkPipelineShaderStageCreateInfo, 2> customShaderStages;

    VkGraphicsPipelineCreateInfo customPipelineCI =
        vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
    customPipelineCI.pInputAssemblyState = &CustominputAssemblyState;
    customPipelineCI.pRasterizationState = &rasterizationState;
    customPipelineCI.pColorBlendState = &colorBlendState;
    customPipelineCI.pMultisampleState = &multisampleState;
    customPipelineCI.pViewportState = &viewportState;
    customPipelineCI.pDepthStencilState = &depthStencilState;
    customPipelineCI.pDynamicState = &dynamicState;
    customPipelineCI.stageCount =
        static_cast<uint32_t>(customShaderStages.size());
    customPipelineCI.pStages = customShaderStages.data();

    // step 12
    customPipelineCI.pVertexInputState =
        vkglTF::Vertex::getPipelineVertexInputState(
            {vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal,
             vkglTF::VertexComponent::Color});

    customPipelineCI.pVertexInputState = &particleVertexInputState;

    // step 5
    customShaderStages[0] =
        //    loadShader(getShadersPath() + "custom/custom_model.vert.spv",
        //               VK_SHADER_STAGE_VERTEX_BIT);
        // loadShader(getShadersPath() + "custom/particle_system.vert.spv",
        //           VK_SHADER_STAGE_VERTEX_BIT);
        loadShader(getShadersPath() + "custom/particles.vert.spv",
                   VK_SHADER_STAGE_VERTEX_BIT);
    customShaderStages[1] =
        //    loadShader(getShadersPath() + "custom/custom_model.frag.spv",
        //               VK_SHADER_STAGE_FRAGMENT_BIT);
        // loadShader(getShadersPath() + "custom/particle_system.frag.spv",
        //           VK_SHADER_STAGE_FRAGMENT_BIT);
        loadShader(getShadersPath() + "custom/particles.frag.spv",
                   VK_SHADER_STAGE_FRAGMENT_BIT);

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(
        device, pipelineCache, 1, &customPipelineCI, nullptr, &customPipeline));
    */
  }

  // Prepare and initialize uniform buffer containing shader uniforms
  void prepareUniformBuffers() {
    // Vertex shader uniform buffer block
    VK_CHECK_RESULT(
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &uniformBufferVS, sizeof(uboVS), &uboVS));

    updateUniformBuffers();
  }

  void updateUniformBuffers() {
    // Vertex shader
    uboVS.projection = camera.matrices.perspective;
    uboVS.modelview = camera.matrices.view * glm::mat4(1.0f);

    // Light source
    if (uiSettings.animateLight) {
      uiSettings.lightTimer += frameTimer * uiSettings.lightSpeed;
      uboVS.lightPos.x =
          sin(glm::radians(uiSettings.lightTimer * 360.0f)) * 15.0f;
      uboVS.lightPos.z =
          cos(glm::radians(uiSettings.lightTimer * 360.0f)) * 15.0f;
    };

    VK_CHECK_RESULT(uniformBufferVS.map());
    memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
    uniformBufferVS.unmap();
  }

  // step 4
  // can instead be placed at updateUniformBuffers, so should be ran at the same
  // time, or at prepareParticles
  void updateParticles(float deltaTime) {
    for (auto &p : particles) {
      p.position += p.velocity * deltaTime; // Move particle rightward

      // Reset the particle once it moves past the right edge (1.0 in NDC space)
      if (p.position.x > 1.0f) {
        float xOffset = static_cast<float>(rand()) / RAND_MAX * 0.2f;
        float yOffset = static_cast<float>(rand()) / RAND_MAX * 0.2f;

        p.position = glm::vec4(-1.0f + xOffset, -1.0f + yOffset, 0.0f,
                               1.0f); // Reset to top-left
      }
    }

    // Upload updated particle data to GPU
    void *data;
    vkMapMemory(device, particleBuffer.memory, 0,
                sizeof(Particle) * particles.size(), 0, &data);
    memcpy(data, particles.data(), sizeof(Particle) * particles.size());
    vkUnmapMemory(device, particleBuffer.memory);
    // the current implementation kinda sucks, running on a CPU, and it takes
    // linear time, fix this some other day
  }

  // step 3
  // can instead be placed in prepareUniformBuffers, so should be ran at the
  // same time
  void prepareParticles() {
    const int PARTICLE_COUNT = 100; // A small number for clear visualization
    particles.resize(PARTICLE_COUNT);
    // Random offsets for starting position near the top-left (-1.0, 1.0)
    float xOffset = static_cast<float>(rand()) / RAND_MAX *
                    0.2f; // Random X offset between 0 and 0.2
    float yOffset = static_cast<float>(rand()) / RAND_MAX *
                    0.2f; // Random Y offset between 0 and 0.2

    for (auto &p : particles) {
      // Start from the top-left corner (-1.0f, -1.0f in NDC space)
      p.position = glm::vec4(-1.0f + xOffset, -1.0f + yOffset, 0.0f, 1.0f);

      // Move diagonally down to the right at a constant speed
      p.velocity = glm::vec4(0.5f, 0.5f, 0.0f, 0.0f);

      p.life =
          5.0f; // Arbitrary lifespan for testing (will reset automatically)
    }

    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &particleBuffer, sizeof(Particle) * particles.size(),
        particles.data()));
  }

  void draw() {
    VulkanExampleBase::prepareFrame();
    buildCommandBuffers();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VulkanExampleBase::submitFrame();
  }

  void loadAssets() {
    const uint32_t glTFLoadingFlags =
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::PreMultiplyVertexColors |
        vkglTF::FileLoadingFlags::FlipY;
    models.models.loadFromFile(getAssetPath() + "models/vulkanscenemodels.gltf",
                               vulkanDevice, queue, glTFLoadingFlags);
    models.background.loadFromFile(getAssetPath() +
                                       "models/vulkanscenebackground.gltf",
                                   vulkanDevice, queue, glTFLoadingFlags);
    models.logos.loadFromFile(getAssetPath() + "models/vulkanscenelogos.gltf",
                              vulkanDevice, queue, glTFLoadingFlags);
    // step 1
    //models.customModel.loadFromFile(getAssetPath() + "models/custom_model.gltf", vulkanDevice, queue, glTFLoadingFlags);
    models.customModel.loadFromFile(getAssetPath() + "models/aero.gltf" , vulkanDevice, queue, glTFLoadingFlags);
    //models.customModel.loadFromFile(getAssetPath() + "models/DataAcqLeaderBoard.gltf" , vulkanDevice, queue, glTFLoadingFlags);

  }

  void prepareImGui() {
    gui = new GUI(this);
    gui->init((float)width, (float)height);
    gui->initResources(renderPass, queue, getShadersPath());
  }

  void prepare() {
    VulkanExampleBase::prepare();
    //loadAssets();
    prepareUniformBuffers();
    // step 8
    // prepareParticles();
    setupLayoutsAndDescriptors();
    preparePipelines();
    prepareImGui();
    buildCommandBuffers();
    prepared = true;
  }

  virtual void render() { // where the magic happens
    if (!prepared)
      return;

    updateUniformBuffers();
    // step 9
    // need to rework this, try to get it full on the GPU
    // this should be a stretch goal / treat for you
    // work on this later :)
    // updateParticles(frameTimer);

    // ImGui input/output
    ImGuiIO &io = ImGui::GetIO();

    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = frameTimer;

    io.MousePos = ImVec2(mouseState.position.x, mouseState.position.y);
    io.MouseDown[0] = mouseState.buttons.left && ui.visible;
    io.MouseDown[1] = mouseState.buttons.right && ui.visible;
    io.MouseDown[2] = mouseState.buttons.middle && ui.visible;

    draw();
  }

  virtual void mouseMoved(double x, double y, bool &handled) {
    ImGuiIO &io = ImGui::GetIO();
    handled = io.WantCaptureMouse && ui.visible;
  }

// Input handling is platform specific, to show how it's basically done this
// sample implements it for Windows
#if defined(_WIN32)
  virtual void OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam) {
    ImGuiIO &io = ImGui::GetIO();
    // Only react to keyboard input if ImGui is active
    if (io.WantCaptureKeyboard) {
      // Character input
      if (uMsg == WM_CHAR) {
        if (wParam > 0 && wParam < 0x10000) {
          io.AddInputCharacter((unsigned short)wParam);
        }
      }
      // Special keys (tab, cursor, etc.)
      if ((wParam < 256) && (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN)) {
        io.KeysDown[wParam] = true;
      }
      if ((wParam < 256) && (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)) {
        io.KeysDown[wParam] = false;
      }
    }
  }
#endif
};

// Main Entrypoints
PHOTON_MAIN()
