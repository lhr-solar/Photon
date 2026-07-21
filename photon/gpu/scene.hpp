#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "gltf.hpp"

struct Position {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct ScenePushConstants {
  float resolution[2];
  float time;
  float _pad;
  glm::mat4 model{1.0f};
};

struct alignas(16) SceneViewProjection {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
  glm::vec4 camPos{0.0f};
};

enum SceneDynamicPart : uint32_t {
  SceneDynamicNone = 0,
  SceneDynamicFrontLeftCorner = 1u << 0,
  SceneDynamicFrontRightCorner = 1u << 1,
  SceneDynamicFrontLeftWheel = 1u << 2,
  SceneDynamicFrontRightWheel = 1u << 3,
  SceneDynamicRearCorner = 1u << 4,
  SceneDynamicRearWheel = 1u << 5,
  SceneDynamicSteeringWheel = 1u << 6,
  SceneDynamicFrontLeftUpperWishbone = 1u << 7,
  SceneDynamicFrontRightUpperWishbone = 1u << 8,
  SceneDynamicFrontLeftLowerWishbone = 1u << 9,
  SceneDynamicFrontRightLowerWishbone = 1u << 10,
  SceneDynamicFrontLeftPushrod = 1u << 11,
  SceneDynamicFrontRightPushrod = 1u << 12,
  SceneDynamicFrontLeftTieRod = 1u << 13,
  SceneDynamicFrontRightTieRod = 1u << 14,
};

struct SceneDynamicsPose {
  float steeringDegrees{0.0f};
  float steeringWheelDegrees{0.0f};
  float frontLeftWheelDegrees{0.0f};
  float frontRightWheelDegrees{0.0f};
  float rearWheelDegrees{0.0f};
  float frontLeftSuspension{0.0f};
  float frontRightSuspension{0.0f};
  float rearSuspension{0.0f};
  float rollDegrees{0.0f};
  float pitchDegrees{0.0f};
};

struct SceneObject {
  std::string name{};
  std::vector<unsigned char> source{};
  tinygltf::TinyGLTF loader{};
  tinygltf::Model model{};
  std::vector<GltfVertex> vertices{};
  std::vector<uint32_t> indices{};
  std::vector<TextureResource> gltfTexturesSrgb{};
  std::vector<TextureResource> gltfTexturesLinear{};
  std::vector<MaterialRuntime> materials{};
  struct DrawItem {
    uint32_t firstIndex{0};
    uint32_t indexCount{0};
    uint32_t materialIndex{0};
    uint32_t dynamicParts{SceneDynamicNone};
  };
  std::vector<DrawItem> drawItems{};
  VkBuffer vertexBuffer{VK_NULL_HANDLE};
  VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};
  VkBuffer indexBuffer{VK_NULL_HANDLE};
  VkDeviceMemory indexBufferMemory{VK_NULL_HANDLE};
  std::vector<VkBuffer> materialUniformBuffers{};
  std::vector<VkDeviceMemory> materialUniformMemories{};
  std::vector<VkDescriptorSet> materialDescriptorSets{};
  Position position{};
  float rotationDegrees{0.0f};
  bool trackable{false};
  bool dynamicsModel{false};
  SceneDynamicsPose dynamics{};
  bool loaded{false};
};

struct SceneFrame {
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
  VkDescriptorSet frameDescriptorSet{VK_NULL_HANDLE};
  VkDescriptorSet postDescriptorSet{VK_NULL_HANDLE};
  VkExtent2D extent{640, 640};
  VkBuffer uniformBuffer{VK_NULL_HANDLE};
  VkDeviceMemory uniformMemory{VK_NULL_HANDLE};
  void* uniformMapped{};
  VkImage sceneMsaaColorImage{VK_NULL_HANDLE};
  VkDeviceMemory sceneMsaaColorMemory{VK_NULL_HANDLE};
  VkImageView sceneMsaaColorView{VK_NULL_HANDLE};
  VkImage sceneColorImage{VK_NULL_HANDLE};
  VkDeviceMemory sceneColorMemory{VK_NULL_HANDLE};
  VkImageView sceneColorView{VK_NULL_HANDLE};
  VkImage sceneDepthImage{VK_NULL_HANDLE};
  VkDeviceMemory sceneDepthMemory{VK_NULL_HANDLE};
  VkImageView sceneDepthView{VK_NULL_HANDLE};
  VkImage outputImage{VK_NULL_HANDLE};
  VkDeviceMemory outputMemory{VK_NULL_HANDLE};
  VkImageView outputView{VK_NULL_HANDLE};
  VkFramebuffer sceneFramebuffer{VK_NULL_HANDLE};
  VkFramebuffer postFramebuffer{VK_NULL_HANDLE};
  bool initialized{};
  unsigned long long texture{};
};

struct Scene {
  GPU* gpu{};
  std::vector<SceneObject> objects{};
  Camera camera{};
  int trackedObjectIndex{-1};
  TextureResource fallbackWhiteTexture{};
  VkDevice device{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
  VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
  VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorPool internalDescriptorPool{VK_NULL_HANDLE};
  VkDescriptorSetLayout uniformDescriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorSetLayout postDescriptorSetLayout{VK_NULL_HANDLE};
  VkRenderPass renderPass{VK_NULL_HANDLE};
  VkRenderPass postRenderPass{VK_NULL_HANDLE};
  TextureResource environmentIrradianceTexture{};
  TextureResource environmentSpecularTexture{};
  VkPipelineLayout scenePipelineLayout{VK_NULL_HANDLE};
  VkPipelineLayout postPipelineLayout{VK_NULL_HANDLE};
  VkPipeline scenePipeline{VK_NULL_HANDLE};
  VkPipeline postPipeline{VK_NULL_HANDLE};
  VkShaderModule sceneFragModule{VK_NULL_HANDLE};
  VkShaderModule sceneVertModule{VK_NULL_HANDLE};
  VkShaderModule postFragModule{VK_NULL_HANDLE};
  VkShaderModule postVertModule{VK_NULL_HANDLE};
  VkSampler offscreenColorSampler{VK_NULL_HANDLE};
  VkVertexInputBindingDescription vertexBindingDescription{};
  std::array<VkVertexInputAttributeDescription, 4> vertexAttributeDescriptions{};
  VkFormat sceneColorFormat{VK_FORMAT_R8G8B8A8_UNORM};
  VkFormat sceneDepthFormat{VK_FORMAT_UNDEFINED};
  VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};
  std::vector<SceneFrame> frames{};
  std::atomic<bool> initialized{};
  std::atomic<bool> partInitialized{};
  bool dirty{};
  uint32_t fif{};
  uint32_t* frameIndex{nullptr};
  bool showing = false;

  void addModel(const char* name, const unsigned char* newModel, size_t size,
                bool trackable = false);
  void init(GPU& gpu);
  void dispatchInit(GPU& gpu);
  void prepareInit(GPU& gpu);
  void finishInit(GPU& gpu);
  void render(GPU& gpu, VkCommandBuffer& commandBuffer);
  void rebuild(GPU& gpu);
  void destroy();
};
