#include "gpuGui.hpp"

#include <cinttypes>
#include <cstdio>
#include <iomanip>
#include <sstream>

#include "gui.hpp"
#include "imgui.h"

inline std::string fmtb(uint64_t b) {
  static constexpr std::array<const char*, 6> u{"B", "KB", "MB", "GB", "TB", "PB"};
  double v = b;
  size_t i = 0;
  while (v >= 1024.0 && i < u.size() - 1) {
    v /= 1024.0;
    ++i;
  }
  std::ostringstream s;
  s << std::fixed << std::setprecision(2) << v << ' ' << u[i];
  return s.str();
}

inline std::string fmth(uint64_t h) {
  char buf[32]{};
  snprintf(buf, sizeof(buf), "0x%" PRIx64, h);
  return buf;
}

void gpuGUI::buildUI(GPU& gpu) {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowBgAlpha(0.5);
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar;
  if (ImGui::Begin("GPU Info Window", nullptr, flags)) {
    ImGui::Text("%s", gpu.deviceProperties.deviceName);
    ImGui::SameLine();
    float fSize = ImGui::GetFontSize();
    TextRightAligned("%.1f : (%.1f, %.1f)", fSize, io.DisplaySize.x, io.DisplaySize.y);
    const auto& qFlags =
        VkQueueFlagsToString(gpu.deviceQueueFamilyProperties[gpu.queueFamilyIndex].queueFlags);
    for (const auto& s : qFlags) {
      ImGui::Text("%s", s.c_str());
      ImGui::SameLine();
    }
    TextRightAligned("%.0f FPS", io.Framerate);

    auto& memTypeCount = gpu.deviceMemoryProperties.memoryTypeCount;
    auto& heapCount = gpu.deviceMemoryProperties.memoryHeapCount;
    for (auto i{0uz}; i < heapCount; i++) {
      const auto& heap = gpu.deviceMemoryProperties.memoryHeaps[i];
      const auto& v = VkMemoryHeapFlagsToString(heap.flags);
      VkDeviceSize used{};
      for (auto j{0uz}; j < gpu.memoryAllocationHeapIndices.size(); ++j) {
        if (gpu.memoryAllocationHeapIndices[j] == i) used += gpu.memoryAllocationSizes[j];
      }
      auto size = fmtb(heap.size);
      auto usedSize = fmtb(used);
      ImGui::Text("Heap Index %i Usage: %s / %s", static_cast<int>(i), usedSize.c_str(),
                  size.c_str());
      ImGui::SameLine();
      for (const auto& s : v) {
        ImGui::Text("%s", s.c_str());
        ImGui::SameLine();
      }
      ImGui::NewLine();
      for (auto j{0uz}; j < memTypeCount; ++j) {
        const auto& memType = gpu.deviceMemoryProperties.memoryTypes[j];
        if (memType.heapIndex != i) continue;
        const auto& propertyFlags = VkMemoryPropertyFlagsToString(memType.propertyFlags);
        ImGui::Indent();
        ImGui::Text("Memory Type %i:", static_cast<int>(j));
        ImGui::SameLine();
        for (const auto& s : propertyFlags) {
          ImGui::Text("%s", s.c_str());
          ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::Unindent();
      }
      ImGui::Separator();
    };

    VkDeviceSize totalBufferBytes{};
    for (const auto& size : gpu.bufferSizes) totalBufferBytes += size;
    ImGui::Text("Buffer Count: %i Total Size: %s", static_cast<int>(gpu.bufferHandles.size()),
                fmtb(totalBufferBytes).c_str());
    for (auto i{0uz}; i < gpu.bufferHandles.size(); ++i) {
      ImGui::Text("Buffer %i Size: %s", static_cast<int>(i), fmtb(gpu.bufferSizes[i]).c_str());
      ImGui::SameLine();
      for (const auto& s : VkBufferUsageFlagsToString(gpu.bufferUsageFlags[i])) {
        ImGui::Text("%s", s.c_str());
        ImGui::SameLine();
      }
      ImGui::NewLine();
    }
    ImGui::Separator();

    ImGui::Text("Image Count: %i", static_cast<int>(gpu.imageHandles.size()));
    for (auto i{0uz}; i < gpu.imageHandles.size(); ++i) {
      const auto& extent = gpu.imageExtents[i];
      ImGui::Text("Image %i: %ux%ux%u %s %s", static_cast<int>(i), extent.width, extent.height,
                  extent.depth, VkFormatToString(gpu.imageFormats[i]),
                  VkSampleCountFlagBitsToString(gpu.imageSampleCounts[i]));
      ImGui::SameLine();
      for (const auto& s : VkImageUsageFlagsToString(gpu.imageUsageFlags[i])) {
        ImGui::Text("%s", s.c_str());
        ImGui::SameLine();
      }
      ImGui::NewLine();
    }
    ImGui::Separator();

    ImGui::Text("Descriptor Pool Count: %i", static_cast<int>(gpu.descriptorPoolHandles.size()));
    for (auto i{0uz}; i < gpu.descriptorPoolHandles.size(); ++i) {
      ImGui::Text("Descriptor Pool %i Max Sets: %u", static_cast<int>(i),
                  gpu.descriptorPoolMaxSets[i]);
      ImGui::SameLine();
      for (const auto& s : VkDescriptorPoolCreateFlagsToString(gpu.descriptorPoolFlags[i])) {
        ImGui::Text("%s", s.c_str());
        ImGui::SameLine();
      }
      ImGui::NewLine();
    }
    ImGui::Separator();

    ImGui::Text("Descriptor Set Layout Count: %i",
                static_cast<int>(gpu.descriptorSetLayoutHandles.size()));
    for (auto i{0uz}; i < gpu.descriptorSetLayoutHandles.size(); ++i) {
      ImGui::Text("Descriptor Set Layout %i Binding Count: %u", static_cast<int>(i),
                  gpu.descriptorSetLayoutBindingCounts[i]);
    }
    ImGui::Separator();

    ImGui::Text("Descriptor Set Count: %i", static_cast<int>(gpu.descriptorSetHandles.size()));
    for (auto i{0uz}; i < gpu.descriptorSetHandles.size(); ++i) {
      ImGui::Text("Descriptor Set %i Pool: %s Layout: %s", static_cast<int>(i),
                  fmth(reinterpret_cast<uint64_t>(gpu.descriptorSetPoolHandles[i])).c_str(),
                  fmth(reinterpret_cast<uint64_t>(gpu.descriptorSetLayoutRefs[i])).c_str());
    }
    ImGui::Separator();

    ImGui::Text("Command Pool Count: %i", static_cast<int>(gpu.commandPoolHandles.size()));
    for (auto i{0uz}; i < gpu.commandPoolHandles.size(); ++i) {
      ImGui::Text("Command Pool %i Queue Family: %u", static_cast<int>(i),
                  gpu.commandPoolQueueFamilyIndices[i]);
      ImGui::SameLine();
      for (const auto& s : VkCommandPoolCreateFlagsToString(gpu.commandPoolFlags[i])) {
        ImGui::Text("%s", s.c_str());
        ImGui::SameLine();
      }
      ImGui::NewLine();
    }
    ImGui::Separator();

    ImGui::Text("Command Buffer Count: %i", static_cast<int>(gpu.commandBufferHandles.size()));
    for (auto i{0uz}; i < gpu.commandBufferHandles.size(); ++i) {
      ImGui::Text("Command Buffer %i Pool: %s Level: %s", static_cast<int>(i),
                  fmth(reinterpret_cast<uint64_t>(gpu.commandBufferPoolHandles[i])).c_str(),
                  VkCommandBufferLevelToString(gpu.commandBufferLevels[i]));
    }
  }
  ImGui::End();
};

std::vector<std::string> gpuGUI::VkMemoryPropertyFlagsToString(VkMemoryPropertyFlags flags) {
  std::vector<std::string> names{};
  if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    names.emplace_back("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT");
  if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    names.emplace_back("VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
  if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    names.emplace_back("VK_MEMORY_PROPERTY_HOST_COHERENT_BIT");
  if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
    names.emplace_back("VK_MEMORY_PROPERTY_HOST_CACHED_BIT");
  if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
    names.emplace_back("VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT");
  if (flags & VK_MEMORY_PROPERTY_PROTECTED_BIT)
    names.emplace_back("VK_MEMORY_PROPERTY_PROTECTED_BIT");
  if (flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD)
    names.emplace_back("VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD");
  if (flags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD)
    names.emplace_back("VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD");
  if (flags & VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV)
    names.emplace_back("VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV");
  return names;
}

std::vector<std::string> gpuGUI::VkMemoryHeapFlagsToString(VkMemoryHeapFlags flags) {
  std::vector<std::string> names{};
  if (flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
    names.emplace_back("VK_MEMORY_HEAP_DEVICE_LOCAL_BIT");
  if (flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
    names.emplace_back("VK_MEMORY_HEAP_MULTI_INSTANCE_BIT");
  return names;
}

std::vector<std::string> gpuGUI::VkQueueFlagsToString(VkQueueFlags flags) {
  std::vector<std::string> names{};
  if (flags & VK_QUEUE_GRAPHICS_BIT) names.emplace_back("VK_QUEUE_GRAPHICS_BIT");
  if (flags & VK_QUEUE_COMPUTE_BIT) names.emplace_back("VK_QUEUE_COMPUTE_BIT");
  if (flags & VK_QUEUE_TRANSFER_BIT) names.emplace_back("VK_QUEUE_TRANSFER_BIT");
  if (flags & VK_QUEUE_SPARSE_BINDING_BIT) names.emplace_back("VK_QUEUE_SPARSE_BINDING_BIT");
  if (flags & VK_QUEUE_PROTECTED_BIT) names.emplace_back("VK_QUEUE_PROTECTED_BIT");
  if (flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) names.emplace_back("VK_QUEUE_VIDEO_DECODE_BIT_KHR");
  if (flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) names.emplace_back("VK_QUEUE_VIDEO_ENCODE_BIT_KHR");
  if (flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) names.emplace_back("VK_QUEUE_OPTICAL_FLOW_BIT_NV");
  return names;
}

std::vector<std::string> gpuGUI::VkBufferUsageFlagsToString(VkBufferUsageFlags flags) {
  std::vector<std::string> names{};
  if (flags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    names.emplace_back("VK_BUFFER_USAGE_TRANSFER_SRC_BIT");
  if (flags & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
    names.emplace_back("VK_BUFFER_USAGE_TRANSFER_DST_BIT");
  if (flags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT");
  if (flags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT");
  if (flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT");
  if (flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_STORAGE_BUFFER_BIT");
  if (flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_INDEX_BUFFER_BIT");
  if (flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT");
  if (flags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
    names.emplace_back("VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT");
  return names;
}

std::vector<std::string> gpuGUI::VkImageUsageFlagsToString(VkImageUsageFlags flags) {
  std::vector<std::string> names{};
  if (flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    names.emplace_back("VK_IMAGE_USAGE_TRANSFER_SRC_BIT");
  if (flags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    names.emplace_back("VK_IMAGE_USAGE_TRANSFER_DST_BIT");
  if (flags & VK_IMAGE_USAGE_SAMPLED_BIT) names.emplace_back("VK_IMAGE_USAGE_SAMPLED_BIT");
  if (flags & VK_IMAGE_USAGE_STORAGE_BIT) names.emplace_back("VK_IMAGE_USAGE_STORAGE_BIT");
  if (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    names.emplace_back("VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT");
  if (flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    names.emplace_back("VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT");
  if (flags & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
    names.emplace_back("VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT");
  if (flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
    names.emplace_back("VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT");
  return names;
}

std::vector<std::string> gpuGUI::VkDescriptorPoolCreateFlagsToString(
    VkDescriptorPoolCreateFlags flags) {
  std::vector<std::string> names{};
  if (flags == 0) names.emplace_back("VK_DESCRIPTOR_POOL_CREATE_NONE");
  if (flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
    names.emplace_back("VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT");
  if (flags & VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
    names.emplace_back("VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT");
  if (flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT)
    names.emplace_back("VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT");
  return names;
}

std::vector<std::string> gpuGUI::VkCommandPoolCreateFlagsToString(VkCommandPoolCreateFlags flags) {
  std::vector<std::string> names{};
  if (flags == 0) names.emplace_back("VK_COMMAND_POOL_CREATE_NONE");
  if (flags & VK_COMMAND_POOL_CREATE_TRANSIENT_BIT)
    names.emplace_back("VK_COMMAND_POOL_CREATE_TRANSIENT_BIT");
  if (flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
    names.emplace_back("VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT");
  if (flags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT)
    names.emplace_back("VK_COMMAND_POOL_CREATE_PROTECTED_BIT");
  return names;
}

const char* gpuGUI::VkFormatToString(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8_UNORM:
      return "VK_FORMAT_R8_UNORM";
    case VK_FORMAT_R8G8B8A8_UNORM:
      return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
      return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
      return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_D16_UNORM:
      return "VK_FORMAT_D16_UNORM";
    case VK_FORMAT_D16_UNORM_S8_UINT:
      return "VK_FORMAT_D16_UNORM_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT:
      return "VK_FORMAT_D24_UNORM_S8_UINT";
    case VK_FORMAT_D32_SFLOAT:
      return "VK_FORMAT_D32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return "VK_FORMAT_D32_SFLOAT_S8_UINT";
    default:
      return "VK_FORMAT_UNKNOWN";
  }
}

const char* gpuGUI::VkSampleCountFlagBitsToString(VkSampleCountFlagBits flags) {
  switch (flags) {
    case VK_SAMPLE_COUNT_1_BIT:
      return "VK_SAMPLE_COUNT_1_BIT";
    case VK_SAMPLE_COUNT_2_BIT:
      return "VK_SAMPLE_COUNT_2_BIT";
    case VK_SAMPLE_COUNT_4_BIT:
      return "VK_SAMPLE_COUNT_4_BIT";
    case VK_SAMPLE_COUNT_8_BIT:
      return "VK_SAMPLE_COUNT_8_BIT";
    case VK_SAMPLE_COUNT_16_BIT:
      return "VK_SAMPLE_COUNT_16_BIT";
    case VK_SAMPLE_COUNT_32_BIT:
      return "VK_SAMPLE_COUNT_32_BIT";
    case VK_SAMPLE_COUNT_64_BIT:
      return "VK_SAMPLE_COUNT_64_BIT";
    default:
      return "VK_SAMPLE_COUNT_UNKNOWN";
  }
}

const char* gpuGUI::VkCommandBufferLevelToString(VkCommandBufferLevel level) {
  switch (level) {
    case VK_COMMAND_BUFFER_LEVEL_PRIMARY:
      return "VK_COMMAND_BUFFER_LEVEL_PRIMARY";
    case VK_COMMAND_BUFFER_LEVEL_SECONDARY:
      return "VK_COMMAND_BUFFER_LEVEL_SECONDARY";
    default:
      return "VK_COMMAND_BUFFER_LEVEL_UNKNOWN";
  }
}
