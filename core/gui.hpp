// ----------------------------------------------------------------------------
// GUI
// ----------------------------------------------------------------------------

#include "VulkanInitializers.hpp"
#include "VulkanglTFModel.h"
#include "imgui_internal.h"
#include "vulkanexamplebase.h"
#include "windows.hpp"
#include <csignal>
#include <imgui.h>
#include <implot.h>
#include <implot3d.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "backend.hpp"
#include <thread>
#include <cstdio>
#include <deque>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan_core.h>
#include <cstdlib>
#include <fstream>
#include <optional>
#include "ui_vert_spv.hpp"
#include "ui_frag_spv.hpp"
#include "Inter_ttf.hpp"
#include "signal_routing.hpp"
#include "implot.h"
#include "config.hpp"

enum class PlotSize { Large, Medium, Small };

static ImVec2 get_plot_size(PlotSize size) {
    switch (size) {
        case PlotSize::Large:  return ImVec2(600, 400);
        case PlotSize::Small:  return ImVec2(200, 150);
        default:               return ImVec2(350, 250);
    }
}

static std::unordered_map<std::string, PlotSize> plot_size_map;


static void perform_app_update() {
    // if no args, nothing to do
    if (VulkanExampleBase::args.empty())
        return;

    // 1) paths
    const std::string exePath = VulkanExampleBase::args[0];
    const std::string newPath = exePath + ".new";
    const std::string url = "https://github.com/RomeroPablo/CAN_sim/releases/"
        "download/pre-release/Photon.exe";


    // 2) download
    const std::string curlCmd = "curl -L -o \"" + newPath + "\" " + url;
    if (std::system(curlCmd.c_str()) != 0) {
        std::remove(newPath.c_str());
        return;
    }

#ifdef _WIN32
    // 4a) Windows: write a .bat that waits, swaps, then starts EXE with args
    const std::string batPath = exePath + "_update.bat";
    {
        std::ofstream bat(batPath);
        bat << "@echo off\n";
        bat << "timeout /T 1 /NOBREAK > NUL\n";
        bat << "del \"" << exePath << "\"\n";
        bat << "move \"" << newPath << "\" \"" << exePath << "\"\n";
        // this start will reopen your app with any args
        bat << "start \"\" \"" << exePath << "\" " << "\n";
    }
    // invoke the batch in its own window
    std::string invoke = "cmd /C start \"\" \"" + batPath + "\"";
    std::system(invoke.c_str());
#else
    // 4b) *nix: write a shell script that waits, swaps, chmods, then execs EXE with args
    const std::string shPath = exePath + "_update.sh";
    {
        std::ofstream sh(shPath);
        sh << "#!/bin/sh\n";
        sh << "sleep 1\n";
        sh << "rm \"" << exePath << "\"\n";
        sh << "mv \"" << newPath << "\" \"" << exePath << "\"\n";
        sh << "chmod +x \"" << exePath << "\"\n";
        // exec so this script is replaced by your app process
        sh << "exec \"" << exePath << "\" " << " &\n";
    }
    // fire it off in background
    std::string invoke = "/bin/sh \"" + shPath + "\" &";
    std::system(invoke.c_str());
#endif

    // 5) exit so the helper can overwrite this binary
    std::exit(0);
}

// Options and values to display/toggle from the UI
struct UISettings {
  bool displayModels = false;
  bool displayLogos = false;
  bool displayBackground = false;
  bool displayCustomModel = false; // NEW
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

static const ImU32 palette[] = {
    IM_COL32( 80,150,255,255), // light-blue
    IM_COL32(255,100,200,255), // pink
    IM_COL32(180, 80,255,255), // purple
    IM_COL32(100,255,150,255), // light-green
    IM_COL32(255,255,255,255)  // white
};

// store history of decoded signal values for plotting
static std::unordered_map<std::string, std::vector<double>> signal_times;
static std::unordered_map<std::string, std::vector<double>> signal_values;
static constexpr size_t MAX_SIGNAL_HISTORY = 500;

static void render_plot_dock(const char* dock_id_str, std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>>& plots){
    ImGuiID dock_id = ImGui::GetID(dock_id_str);
    ImGui::DockSpace(dock_id);

    static std::unordered_map<ImGuiID, bool> initialized;
    if(!initialized[dock_id]){
        initialized[dock_id] = true;
        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id);
        for(const auto &pl : plots){
            ImGui::DockBuilderDockWindow(pl.first.c_str(), dock_id);
        }
        ImGui::DockBuilderFinish(dock_id);
    }

    for(auto &pl : plots){
        PlotSize size = PlotSize::Medium;
        auto it = plot_size_map.find(pl.first);
        if(it != plot_size_map.end())
            size = it->second;
        ImGui::SetNextWindowSize(get_plot_size(size), ImGuiCond_FirstUseEver);
        ImGui::Begin(pl.first.c_str());
        auto drawer = g_plot_drawers.get_drawer(pl.first);
        drawer(pl.first, pl.second, signal_times, signal_values);
        ImGui::End();
    }
}

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

  VkImage sceneFallbackImage = VK_NULL_HANDLE;
  VkDeviceMemory sceneFallbackMemory = VK_NULL_HANDLE;
  VkImageView sceneView = VK_NULL_HANDLE;
  bool ownsSceneView = false;

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

  // gui configuration
  Windows configuration;
  Windows visualization;
  Windows demo;
  std::vector<Windows> tabs;
  int config_idx = 0;
  int vis_idx = 1;

  std::thread backend_thread;

  int main_tab_idx = 0;
  bool show_dbc_config = false;

public:
  // UI params are set via push constants
  ImVec2 modelWindowPos = ImVec2(0, 0);
  ImVec2 modelWindowSize = ImVec2(0, 0);

  struct PushConstBlock {
    glm::vec2 scale;
    glm::vec2 translate;
    glm::vec2 gradTop;
    glm::vec2 gradBottom;
    glm::vec2 invScreenSize;
    glm::vec2 whitePixel;
    float u_time;
  } pushConstBlock;

  GUI(VulkanExampleBase *example) : example(example) {
    device = example->vulkanDevice;
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();

    init_default_plot_registry();
    init_default_plot_drawers();

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
    if (ownsSceneView) {
      vkDestroyImage(device->logicalDevice, sceneFallbackImage, nullptr);
      vkFreeMemory(device->logicalDevice, sceneFallbackMemory, nullptr);
      vkDestroyImageView(device->logicalDevice, sceneView, nullptr);
    }
    vkDestroySampler(device->logicalDevice, sampler, nullptr);
    vkDestroyPipelineCache(device->logicalDevice, pipelineCache, nullptr);
    vkDestroyPipeline(device->logicalDevice, pipeline, nullptr);
    //vkDestroyPipelineLayout(device->logicalDevice, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayout,
                                 nullptr);

    if (backend_thread.joinable())
        backend_thread.join();
  }

  // Initialize styles, keys, etc.
  void init(float width, float height) {
      // delete update script
#ifdef _WIN32
      std::string update_script = "Photon.exe_update.bat";
      std::remove(update_script.c_str());
#endif
    // Color scheme
    PhotonStyle = ImGui::GetStyle();
    ImVec4 *colors = PhotonStyle.Colors;

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.9f); 
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.05f, 0.05f, 0.05f, 0.9f);

    // Borders and separators
    colors[ImGuiCol_Border] =
        ImVec4(0.2f, 0.2f, 0.2f, 1.0f); 
    colors[ImGuiCol_Separator] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    // Text colors
    colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); 
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f); 

    // Headers and title
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f); 
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.25f, 0.25f, 0.25f, 1.0f); 
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.3f, 0.3f, 0.3f, 1.0f); 

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
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

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
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDockingNoSplit =
        false; // Disable splitting (what's the point then?)
    io.ConfigDockingAlwaysTabBar = false; // Enable for only tab bars (ew)

    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#if defined(_WIN32)
    /*
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
    */
#endif

    io.IniFilename = nullptr;

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

  void ApplyModernPhotonStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* C = style.Colors;

    // — Rounded corners everywhere —
    style.WindowRounding    = 10.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 5.0f;
    style.TabRounding       = 4.0f;

    // — Padding & spacing —
    style.WindowPadding     = {15, 15};
    style.FramePadding      = {10, 6};
    style.ItemSpacing       = {10, 8};
    style.ItemInnerSpacing  = {6, 6};

    // — Base colors (glass-like) —
    ImVec4 bg        = {0.03f, 0.03f, 0.05f, 0.85f}; 
    ImVec4 childBg   = {0, 0, 0, 0};                
    ImVec4 popupBg   = {0.12f, 0.12f, 0.15f, 0.9f};
    ImVec4 border    = {0.20f, 0.20f, 0.25f, 1.0f};
    ImVec4 separator = {0.30f, 0.30f, 0.35f, 1.0f};

    C[ImGuiCol_WindowBg]            = bg;
    C[ImGuiCol_ChildBg]             = childBg;
    C[ImGuiCol_PopupBg]             = popupBg;
    C[ImGuiCol_Border]              = border;
    C[ImGuiCol_Separator]           = separator;

    // — Text —
    C[ImGuiCol_Text]                = {0.92f, 0.92f, 0.95f, 1.0f};
    C[ImGuiCol_TextDisabled]        = {0.55f, 0.55f, 0.60f, 1.0f};

    // — Accent gradient colors —
    ImVec4 accent       = {0.18f, 0.55f, 0.70f, 0.8f};   // base
    ImVec4 accentHover  = {0.14f, 0.45f, 0.60f, 0.8f};
    ImVec4 accentActive = {0.10f, 0.34f, 0.45f, 0.8f};

    // Buttons
    C[ImGuiCol_Button]             = accent;
    C[ImGuiCol_ButtonHovered]      = accentHover;
    C[ImGuiCol_ButtonActive]       = accentActive;

    // Headers (e.g. tree nodes, collapsibles)
    C[ImGuiCol_Header]             = accent;
    C[ImGuiCol_HeaderHovered]      = accentHover;
    C[ImGuiCol_HeaderActive]       = accentActive;

    // Frame (inputs, sliders)
    C[ImGuiCol_FrameBg]            = {0.10f, 0.10f, 0.12f, 0.8f};
    C[ImGuiCol_FrameBgHovered]     = accentHover;
    C[ImGuiCol_FrameBgActive]      = accentActive;
    C[ImGuiCol_SliderGrab]         = accent;
    C[ImGuiCol_SliderGrabActive]   = accentActive;
    C[ImGuiCol_CheckMark]          = {1.0f, 1.0f, 1.0f, 1.0f};

    // Tabs
    C[ImGuiCol_Tab]                = {0.10f, 0.10f, 0.12f, 0.8f};
    C[ImGuiCol_TabHovered]         = accentHover;
    C[ImGuiCol_TabActive]          = accentActive;
    C[ImGuiCol_TabUnfocused]       = {0.05f, 0.05f, 0.07f, 0.7f};
    C[ImGuiCol_TabUnfocusedActive] = {0.08f, 0.08f, 0.10f, 0.7f};

    // Scrollbars
    C[ImGuiCol_ScrollbarBg]        = {0.00f, 0.00f, 0.00f, 0.5f};
    C[ImGuiCol_ScrollbarGrab]      = accent;
    C[ImGuiCol_ScrollbarGrabHovered] = accentHover;
    C[ImGuiCol_ScrollbarGrabActive]  = accentActive;

    // Plot lines & histograms
    C[ImGuiCol_PlotLines]          = accent;
    C[ImGuiCol_PlotLinesHovered]   = {1.0f,1.0f,1.0f,1.0f};
    C[ImGuiCol_PlotHistogram]      = accent;
    C[ImGuiCol_PlotHistogramHovered] = accentHover;

    // Misc
    C[ImGuiCol_DragDropTarget]     = {1.0f, 1.0f, 1.0f, 0.9f};
    C[ImGuiCol_NavHighlight]       = accentHover;
    C[ImGuiCol_ModalWindowDimBg]   = {0,0,0,0.7f};
    C[ImGuiCol_DockingEmptyBg]     = {0,0,0,0};

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
                     const std::string &shadersPath, VkImageView sceneImageView = VK_NULL_HANDLE) {
    ImGuiIO &io = ImGui::GetIO();

    // --- init scene view ---
    // If the application supplies a scene texture view, use it directly.
    // Otherwise create a 1x1 black image so the descriptor set remains valid
    // without sampling the font atlas.
    if (sceneImageView != VK_NULL_HANDLE) {
      sceneView = sceneImageView;
      ownsSceneView = false;
    } else {
      ownsSceneView = true;
      VkImageCreateInfo sceneInfo = vks::initializers::imageCreateInfo();
      sceneInfo.imageType = VK_IMAGE_TYPE_2D;
      sceneInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
      sceneInfo.extent.width = 1;
      sceneInfo.extent.height = 1;
      sceneInfo.extent.depth = 1;
      sceneInfo.mipLevels = 1;
      sceneInfo.arrayLayers = 1;
      sceneInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      sceneInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      sceneInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      sceneInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      sceneInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      VK_CHECK_RESULT(
          vkCreateImage(device->logicalDevice, &sceneInfo, nullptr, &sceneFallbackImage));
      VkMemoryRequirements sceneReqs;
      vkGetImageMemoryRequirements(device->logicalDevice, sceneFallbackImage, &sceneReqs);
      VkMemoryAllocateInfo sceneAlloc = vks::initializers::memoryAllocateInfo();
      sceneAlloc.allocationSize = sceneReqs.size;
      sceneAlloc.memoryTypeIndex = device->getMemoryType(
          sceneReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &sceneAlloc, nullptr,
                                       &sceneFallbackMemory));
      VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, sceneFallbackImage,
                                        sceneFallbackMemory, 0));

      VkImageViewCreateInfo sceneViewInfo = vks::initializers::imageViewCreateInfo();
      sceneViewInfo.image = sceneFallbackImage;
      sceneViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      sceneViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
      sceneViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      sceneViewInfo.subresourceRange.levelCount = 1;
      sceneViewInfo.subresourceRange.layerCount = 1;
      VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &sceneViewInfo, nullptr,
                                        &sceneView));

      // Upload a black pixel so sampling returns transparent black
      uint32_t black = 0;
      vks::Buffer sceneStaging;
      VK_CHECK_RESULT(device->createBuffer(
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &sceneStaging, sizeof(uint32_t)));
      sceneStaging.map();
      memcpy(sceneStaging.mapped, &black, sizeof(uint32_t));
      sceneStaging.unmap();
      VkCommandBuffer sceneCmd =
          device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
      vks::tools::setImageLayout(sceneCmd, sceneFallbackImage, VK_IMAGE_ASPECT_COLOR_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_PIPELINE_STAGE_HOST_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkBufferImageCopy sceneCopyRegion{};
      sceneCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      sceneCopyRegion.imageSubresource.layerCount = 1;
      sceneCopyRegion.imageExtent.width = 1;
      sceneCopyRegion.imageExtent.height = 1;
      sceneCopyRegion.imageExtent.depth = 1;
      vkCmdCopyBufferToImage(sceneCmd, sceneStaging.buffer, sceneFallbackImage,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                             &sceneCopyRegion);
      vks::tools::setImageLayout(sceneCmd, sceneFallbackImage, VK_IMAGE_ASPECT_COLOR_BIT,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      device->flushCommandBuffer(sceneCmd, copyQueue, true);
      sceneStaging.destroy();
    }
    // --- end scene view ---

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
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)};
    VkDescriptorPoolCreateInfo descriptorPoolInfo =
        vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
    VK_CHECK_RESULT(vkCreateDescriptorPool(
        device->logicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));

    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        vks::initializers::descriptorSetLayoutBinding(
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
                VK_SHADER_STAGE_FRAGMENT_BIT, 1),
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
    VkDescriptorImageInfo sceneDescriptor = vks::initializers::descriptorImageInfo(
            sampler, fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorImageInfo fontDescriptor =
        vks::initializers::descriptorImageInfo(
            sampler, fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        vks::initializers::writeDescriptorSet(
            descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0,
            &sceneDescriptor),
        vks::initializers::writeDescriptorSet(
            descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
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
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
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

void update_signal_data(){
    static double last_time = -1.0;
    double now = ImGui::GetTime();
    if (now == last_time)
        return;
    last_time = now;

    auto messages = backend_get_messages();
    const CanStore &store = get_can_store();

    for(const auto &mp : messages){
        uint32_t id = mp.first;
        const auto &msg = mp.second;
        CanFrame frame;
        if(store.read(id, frame)){
            std::vector<std::pair<std::string,double>> vals;
            if(backend_decode_signals(id, frame, vals)){
                for(const auto &p : vals){
                    std::string key = msg.dbc_name + ":" + std::to_string(id) + ":" + p.first;
                    auto &xt = signal_times[key];
                    auto &yv = signal_values[key];
                    xt.push_back(now);
                    yv.push_back(p.second);
                    if(xt.size() > MAX_SIGNAL_HISTORY){
                        xt.erase(xt.begin());
                        yv.erase(yv.begin());
                    }
                }
            }
        }
    }
}

std::unordered_map<std::string, int> dbc_idx = {
    {"BPS", 0},
    {"Wavesculptor22", 1},
    {"MPPT", 2},
    {"Controls", 3},
    {"DAQ", 4}
};

void bps_window(){
      auto messages = backend_get_messages();
      std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>> plots;

      for(const auto &mp : messages){
          const auto &msg = mp.second;
          if(msg.dbc_name != "BPS")
              continue;
          for(const auto &sig : mp.second.signals){
              std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + sig.name;
              auto itx = signal_times.find(key);
              auto ity = signal_values.find(key);
              if(itx == signal_times.end() || ity == signal_values.end())
                  continue;
              if(itx->second.empty())
                  continue;
              std::string plot = g_plot_registry.get_plot("BPS", mp.first, sig.name);
              if(plot.empty())
                  plot = sig.name;
              plots[plot].push_back({key, sig.name});
          }
      }
     render_plot_dock("BPSDock", plots);
}

std::optional<double> get_bps_value(const std::unordered_map<uint32_t, DbcMessage>& messages,
    const char* sig) {
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "BPS")
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == sig) {
                std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                auto it = signal_values.find(key);
                if (it != signal_values.end() && !it->second.empty())
                    return it->second.back();
            }
        }
    }
    return std::nullopt;
}

void Sparkline(const char* id, const float* values, int count, float min_v, float max_v, int offset, const ImVec4& col, const ImVec2& size) {
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0,0));
    if (ImPlot::BeginPlot(id,size,ImPlotFlags_CanvasOnly)) {
        ImPlot::SetupAxes(nullptr,nullptr,ImPlotAxisFlags_NoDecorations,ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxesLimits(0, count - 1, min_v, max_v, ImGuiCond_Always);
        ImPlot::SetNextLineStyle(col);
        ImPlot::SetNextFillStyle(col, 0.25);
        ImPlot::PlotLine(id, values, count, 1, 0, ImPlotLineFlags_Shaded, offset);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
}

void custom_bps() {
    auto messages = backend_get_messages();

    struct LightSig { const char* label; const char* name; };
    const LightSig lights[] = {
        {"BPS Trip", "BPS_Trip"},
        {"BPS All Clear", "BPS_All_Clear"},
        {"HV Contactor", "HV_Contactor"},
        {"MPPT Boost Enabled", "MPPT_Boost_Enabled"},
        {"Charge Enabled", "Charge_Enabled"},
        {"Array Contactor", "Array_Contactor"},
    };

    struct PlotData {
        std::vector<double>* xs;
        std::vector<double>* ys;
        std::string plot_name;
    };
    std::vector<PlotData> plot_data;

    double t_min = std::numeric_limits<double>::max();
    double t_max = 0.0;

    for (const auto& light : lights) {
        std::string key;
        for (const auto& mp : messages) {
            const auto& msg = mp.second;
            if (msg.dbc_name != "BPS") continue;
            for (const auto& s : msg.signals) {
                if (s.name == light.name) {
                    key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                    break;
                }
            }
            if (!key.empty()) break;
        }
        if (!key.empty()) {
            auto itx = signal_times.find(key);
            auto ity = signal_values.find(key);
            if (itx != signal_times.end() && ity != signal_values.end() && !itx->second.empty()) {
                plot_data.push_back({ &itx->second, &ity->second, light.label });
                if (!itx->second.empty()) {
                    t_min = std::min(t_min, itx->second.front());
                    t_max = std::max(t_max, itx->second.back());
                }
            }
        }
    }

    ImGui::BeginChild("bps_status_row", ImVec2(0, 260), true);
    if (ImGui::BeginTable("bps_status_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 144.0f);
        ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        for (size_t i = 0; i < std::size(lights); ++i) {
            std::optional<double> v;
            if (i < plot_data.size() && !plot_data[i].ys->empty())
                v = plot_data[i].ys->back();
            bool on = v && *v;
            ImU32 col = on ? IM_COL32(100, 255, 150, 255) : IM_COL32(255, 100, 100, 255);
            float radius = ImGui::GetTextLineHeight() * 0.4f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, col);
            ImGui::SameLine();
            ImGui::Text("%s", lights[i].label);
        }

        ImGui::TableSetColumnIndex(1);

        ImVec2 plot_size = ImVec2(ImGui::GetContentRegionAvail().x, 220);
        double now = ImGui::GetTime();
        double window = 30.0;
        double x0 = (t_max > t_min && t_max - t_min > window) ? t_max - window : t_min;
        double x1 = t_max > t_min ? t_max : now;

        ImPlotFlags plot_flags = ImPlotFlags_NoFrame | ImPlotFlags_NoTitle;
        ImPlotAxisFlags x_flags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoLabel;
        ImPlotAxisFlags y_flags = ImPlotAxisFlags_LockMin | ImPlotAxisFlags_LockMax;

        if (ImPlot::BeginPlot("BPS Status Signals", plot_size, plot_flags)) {
            ImPlot::SetupAxes("Time", "State", x_flags, y_flags);
            ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.5, ImGuiCond_Always);
			ImPlot::SetupAxisFormat(ImAxis_X1, "%.1f"); 
            //ImPlot::SetupAxisFormat(ImAxis_X1, int_tick_formatter, nullptr);
            for (const auto& pd : plot_data) {
                if (!pd.xs->empty() && !pd.ys->empty())
                    ImPlot::PlotLine(pd.plot_name.c_str(), pd.xs->data(), pd.ys->data(), (int)pd.xs->size());
            }
            ImPlot::EndPlot();
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    // --- Summary Row: SoC, Avg Temp, Pack Voltage, Current, Current Sparkline ---
    ImGui::BeginChild("bps_summary_row", ImVec2(0, 80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::BeginTable("bps_summary_table", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(" SoC", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Avg Temp", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Supp. Voltage", ImGuiTableColumnFlags_WidthFixed, 120.0f); // New column
        ImGui::TableSetupColumn("Pack Voltage", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("##Current Sparkline", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        auto messages = backend_get_messages();
        auto soc = get_bps_value(messages, "SoC");
        auto avg_temp = get_bps_value(messages, "Average_Temp");
        auto supp_voltage = get_bps_value(messages, "Supplemental_Voltage");
        auto pack_voltage = get_bps_value(messages, "Pack_Voltage");
        auto current = get_bps_value(messages, "Current");

        // SoC
        ImGui::TableSetColumnIndex(0);
        if (soc)
            ImGui::Text(" %.1f %%", *soc / 1000.0 / 1000.0);
        else
            ImGui::Text("--");

        // Avg Temp
        ImGui::TableSetColumnIndex(1);
        if (avg_temp)
            ImGui::Text("%.1f °C", *avg_temp / 1000.0);
        else
            ImGui::Text("--");

        // Supplemental Voltage (new column)
        ImGui::TableSetColumnIndex(2);
        if (supp_voltage)
            ImGui::Text("%.2f V", *supp_voltage / 1000.0);
        else
            ImGui::Text("--");

        // Pack Voltage
        ImGui::TableSetColumnIndex(3);
        if (pack_voltage)
            ImGui::Text("%.2f V", *pack_voltage / 1000.0);
        else
            ImGui::Text("--");

        // Current (latest value)
        ImGui::TableSetColumnIndex(4);
        if (current)
            ImGui::Text("%.2f A", *current / 1000.0);
        else
            ImGui::Text("--");

        // Current Sparkline
        ImGui::TableSetColumnIndex(5);
        // Find the key for current
        std::string current_key;
        for (const auto& mp : messages) {
            const auto& msg = mp.second;
            if (msg.dbc_name != "BPS") continue;
            for (const auto& s : msg.signals) {
                if (s.name == "Current") {
                    current_key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                    break;
                }
            }
            if (!current_key.empty()) break;
        }
        auto itx = signal_times.find(current_key);
        auto ity = signal_values.find(current_key);
        if (itx != signal_times.end() && ity != signal_values.end() && !itx->second.empty()) {
            const auto& xs = itx->second;
            const auto& ys = ity->second;
            constexpr int N = 100;
            int count = static_cast<int>(xs.size());
            int start = count > N ? count - N : 0;
            ImVec2 spark_size = ImVec2(ImGui::GetContentRegionAvail().x, 35.0f);
            std::vector<float> ysf;
            ysf.reserve(ys.size() - start);
            for (auto it = ys.begin() + start; it != ys.end(); ++it)
                ysf.push_back(static_cast<float>(*it / 1000.0));
            float min_v = 0.0f, max_v = 100.0f;
            int offset = 0;
            ImVec4 spark_col = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
            Sparkline("Current", ysf.data(), static_cast<int>(ysf.size()), min_v, max_v, offset, spark_col, spark_size);
        }
        else {
            ImGui::Text("No current data");
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::BeginChild("bps_modules", ImVec2(0, 0), true);

    const int num_modules = 32;
    const int num_cols = 8;
    // Use static arrays to persist values across frames
    static double cell_volt[num_modules] = { 0.0 };
    static double cell_temp[num_modules] = { 0.0 };

    // Find the keys for the signals
    std::string volt_idx_key, volt_val_key, temp_idx_key, temp_val_key;
    for (std::unordered_map<uint32_t, DbcMessage>::const_iterator mp = messages.begin(); mp != messages.end(); ++mp) {
        const DbcMessage& msg = mp->second;
        if (msg.dbc_name != "BPS") continue;
        for (size_t i = 0; i < msg.signals.size(); ++i) {
            const std::string& sname = msg.signals[i].name;
            if (sname == "Voltage_idx") volt_idx_key = msg.dbc_name + ":" + std::to_string(mp->first) + ":" + sname;
            if (sname == "Voltage_Value") volt_val_key = msg.dbc_name + ":" + std::to_string(mp->first) + ":" + sname;
            if (sname == "Temperature_idx") temp_idx_key = msg.dbc_name + ":" + std::to_string(mp->first) + ":" + sname;
            if (sname == "Temperature_Value") temp_val_key = msg.dbc_name + ":" + std::to_string(mp->first) + ":" + sname;
        }
    }

    // Map latest values to their module index, but only update if new data is available
    if (!volt_idx_key.empty() && !volt_val_key.empty()) {
        auto it_idx = signal_values.find(volt_idx_key);
        auto it_val = signal_values.find(volt_val_key);
        if (it_idx != signal_values.end() && it_val != signal_values.end()) {
            const std::vector<double>& idxs = it_idx->second;
            const std::vector<double>& vals = it_val->second;
            size_t n = std::min(idxs.size(), vals.size());
            for (size_t i = 0; i < n; ++i) {
                int idx = static_cast<int>(idxs[i]);
                if (idx >= 0 && idx < num_modules) {
                    cell_volt[idx] = vals[i] / 1000.0; // mV to V
                }
            }
        }
    }
    if (!temp_idx_key.empty() && !temp_val_key.empty()) {
        auto it_idx = signal_values.find(temp_idx_key);
        auto it_val = signal_values.find(temp_val_key);
        if (it_idx != signal_values.end() && it_val != signal_values.end()) {
            const std::vector<double>& idxs = it_idx->second;
            const std::vector<double>& vals = it_val->second;
            size_t n = std::min(idxs.size(), vals.size());
            for (size_t i = 0; i < n; ++i) {
                int idx = static_cast<int>(idxs[i]);
                if (idx >= 0 && idx < num_modules) {
                    cell_temp[idx] = vals[i] / 1000.0; // mC to C
                }
            }
        }
    }

    if (ImGui::BeginTable("ModTable", num_cols, ImGuiTableFlags_SizingFixedFit)) {
        ImGuiStyle& style = ImGui::GetStyle();
        float avail_height = ImGui::GetContentRegionAvail().y;
        float cell_height = (avail_height / 4.0f) - style.CellPadding.y * 2.0f;
        float avail_width = ImGui::GetContentRegionAvail().x;
        float cell_width = (avail_width / num_cols) - style.CellPadding.x * 2.0f;

        for (int col = 0; col < num_cols; ++col) {
            ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, cell_width);
        }

        for (int pos = 0; pos < num_modules; ++pos) {
            ImGui::TableNextColumn();
            int row = pos / 8;
            int col = pos % 8;
            int moduleNum = (row % 2 == 0) ? (row * 8 + (8 - col)) : (row * 8 + col + 1);
            int dataIdx = moduleNum - 1;
            float tmin = -20.0f, tmax = 60.0f;
            float norm = (cell_temp[dataIdx] - tmin) / (tmax - tmin);
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;
            ImVec4 border_col = ImPlot::SampleColormap(norm, ImPlot3DColormap_Viridis);
            std::ostringstream oss;
            oss << "Module " << std::setw(2) << std::setfill('0') << moduleNum
                << "\n" << std::fixed << std::setprecision(2)
                << cell_volt[dataIdx] << " V\n"
                << cell_temp[dataIdx] << " °C";
            std::string label = oss.str();
            ImVec4 bg = style.Colors[ImGuiCol_FrameBg];
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
            ImGui::PushStyleColor(ImGuiCol_Border, border_col);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

            ImGui::Button(label.c_str(), ImVec2(cell_width, cell_height));

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();


}

void controls_window(){
      auto messages = backend_get_messages();
      std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>> plots;

      for(const auto &mp : messages){
          const auto &msg = mp.second;
          if(msg.dbc_name != "Controls")
              continue;
          for(const auto &sig : mp.second.signals){
              std::string key = msg.dbc_name + ":" + std::to_string(mp.first) +":" + sig.name;
              auto itx = signal_times.find(key);
              auto ity = signal_values.find(key);
              if(itx == signal_times.end() || ity == signal_values.end())
                  continue;
              if(itx->second.empty())
                  continue;
              std::string plot = g_plot_registry.get_plot("Controls", mp.first, sig.name);
              if(plot.empty())
                  plot = sig.name;
              plots[plot].push_back({key, sig.name});
          }
      }
     render_plot_dock("ControlsDock", plots);
}

std::optional<double> get_controls_value(const std::unordered_map<uint32_t, DbcMessage>& messages,
    const char* sig) {
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "Controls")
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == sig) {
                std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                auto it = signal_values.find(key);
                if (it != signal_values.end() && !it->second.empty())
                    return it->second.back();
            }
        }
    }
    return std::nullopt;
}

void custom_controls() {
    auto messages = backend_get_messages();

    auto get = [&](const char* name) { return get_controls_value(messages, name); };

    auto accel = get("Acceleration_Percentage");
    auto brake = get("Brake_Percentage");
    auto ign_array = get("IGN_Array");
    auto ign_motor = get("IGN_Motor");
    auto regen_sw = get("Regen_SW");
    auto fwd = get("Forward_Gear");
    auto rev = get("Reverse_Gear");
    auto cruise_en = get("Cruz_EN");
    auto cruise_set = get("Cruz_Set");
    auto brake_light = get("Brake_Light");

    auto c_fault = get("Controls_Fault");
    auto mc_fault = get("Motor_Controller_Fault");
    auto bps_fault = get("BPS_Fault");
    auto pedals_fault = get("Pedals_Fault");
    auto carcan_fault = get("CarCAN_Fault");
    auto int_fault = get("Internal_Controls_Fault");
    auto os_fault = get("OS_Fault");
    auto lakshay_fault = get("Lakshay_Fault");

    auto motor_safe = get("Motor_Safe");
    auto motor_err = get("Motor_Controller_Error");

    auto cur_set = get("Motor_Current_Setpoint");
    auto vel_set = get("Motor_Velocity_Setpoint");
    auto pwr_set = get("Motor_Power_Setpoint");

    // helper widgets
    auto draw_light = [&](const char* label, std::optional<double> v) {
        bool on = v && *v;
        ImU32 col = on ? IM_COL32(100, 255, 150, 255) : IM_COL32(255, 100, 100, 255);
        float r = ImGui::GetTextLineHeight() * 0.4f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(r * 2, r * 2));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(p.x + r, p.y + r), r, col);
        ImGui::SameLine();
        ImGui::Text("%s", label);
        };

    auto draw_vbar = [&](const char* label, std::optional<double> v) {
        float h = 80.f;
        float w = ImGui::GetFrameHeight() * 0.6f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(200, 200, 200, 255));
        float frac = v ? ImClamp((float)*v / 100.f, 0.f, 1.f) : 0.f;
        dl->AddRectFilled(ImVec2(pos.x, pos.y + h * (1 - frac)), ImVec2(pos.x + w, pos.y + h), IM_COL32(100, 255, 150, 255));
        ImGui::Dummy(ImVec2(w, h));
        char buf[32];
        if (v) snprintf(buf, sizeof(buf), "%s %.0f%%", label, *v);
        else snprintf(buf, sizeof(buf), "%s --", label);
        ImGui::Text("%s", buf);
        };

    auto draw_gauge = [&](const char* label, std::optional<double> v, double max) {
        float r = 35.f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();       // returns ImVec2
        ImVec2 center = ImVec2(cursorPos.x + r,           // x + radius
            cursorPos.y + r);          // y + radius
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float a0 = IM_PI * 0.75f;
        float a1 = IM_PI * 2.25f;
        dl->PathArcTo(center, r, a0, a1, 32);
        dl->PathStroke(IM_COL32(200, 200, 200, 255), false, 2.f);
        float frac = v ? ImClamp((float)(*v / max), 0.f, 1.f) : 0.f;
        float ang = a0 + (a1 - a0) * frac;
        float cx = cosf(ang) * r;                            // x component * radius
        float sy = sinf(ang) * r;                            // y component * radius
        ImVec2 tip = ImVec2(center.x + cx, center.y + sy);
        dl->AddLine(center, tip, IM_COL32(100, 255, 150, 255), 2.0f);
        ImGui::Dummy(ImVec2(r * 2, r * 1.3f));
        char buf[32];
        if (v) snprintf(buf, sizeof(buf), "%s %.0f", label, *v);
        else snprintf(buf, sizeof(buf), "%s --", label);
        ImGui::Text("%s", buf);
        };

    ImGui::BeginChild("ctrl_inputs", ImVec2(0, 140), true);
    ImGui::Columns(2, "input_cols", false);
    // left: vertical pedals
    draw_vbar("Accel", accel);
    draw_vbar("Brake", brake);
    ImGui::NextColumn();
    draw_light("IGN Array", ign_array);
    draw_light("IGN Motor", ign_motor);
    draw_light("Regen", regen_sw);
    draw_light("Forward", fwd);
    draw_light("Reverse", rev);
    draw_light("Cruise EN", cruise_en);
    draw_light("Cruise Set", cruise_set);
    draw_light("Brake Light", brake_light);
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();

    ImGui::BeginChild("ctrl_faults", ImVec2(0, 100), true);
    ImGui::Columns(4, "fault_cols", false);
    draw_light("Ctrl", c_fault); ImGui::NextColumn();
    draw_light("MC", mc_fault); ImGui::NextColumn();
    draw_light("BPS", bps_fault); ImGui::NextColumn();
    draw_light("Pedals", pedals_fault); ImGui::NextColumn();
    draw_light("CarCAN", carcan_fault); ImGui::NextColumn();
    draw_light("Internal", int_fault); ImGui::NextColumn();
    draw_light("OS", os_fault); ImGui::NextColumn();
    draw_light("Lakshay", lakshay_fault);
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();

    ImGui::BeginChild("ctrl_motor", ImVec2(0, 160), true);
    ImGui::Columns(2, "motor_cols", false);
    draw_light("Motor Safe", motor_safe);
    draw_light("MC Error", motor_err);
    ImGui::NextColumn();
    draw_gauge("Current", cur_set, 300.0);
    draw_gauge("Velocity", vel_set, 6000.0);
    draw_gauge("Power", pwr_set, 100.0);
    ImGui::Columns(1);
    ImGui::EndChild();

    // time-series plots for motor commands
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> plots;
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "Controls")
            continue;
        for (const auto& sig : msg.signals) {
            if (sig.name != "Motor_Current_Setpoint" && sig.name != "Motor_Velocity_Setpoint" && sig.name != "Motor_Power_Setpoint")
                continue;
            std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + sig.name;
            auto itx = signal_times.find(key);
            auto ity = signal_values.find(key);
            if (itx == signal_times.end() || ity == signal_values.end())
                continue;
            if (itx->second.empty())
                continue;
            plots[sig.name].push_back({ key, sig.name });
        }
    }
    if (!plots.empty())
        render_plot_dock("ControlsDock", plots);
}


void prohelion_window(){
      auto messages = backend_get_messages();
      std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>> plots;

      for(const auto &mp : messages){
          const auto &msg = mp.second;
          if(msg.dbc_name != "Wavesculptor22")
              continue;
          for(const auto &sig : mp.second.signals){
              std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + sig.name;
              auto itx = signal_times.find(key);
              auto ity = signal_values.find(key);
              if(itx == signal_times.end() || ity == signal_values.end())
                  continue;
              if(itx->second.empty())
                  continue;
              std::string plot = g_plot_registry.get_plot("Wavesculptor22", mp.first, sig.name);
              if(plot.empty())
                  plot = sig.name;
              plots[plot].push_back({key, sig.name});
          }
      }
      render_plot_dock("ProhelionDock", plots);
}

std::optional<double> get_ws_value(const std::unordered_map<uint32_t, DbcMessage>& messages,
    const char* sig) {
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "Wavesculptor22")
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == sig) {
                std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                auto it = signal_values.find(key);
                if (it != signal_values.end() && !it->second.empty())
                    return it->second.back();
            }
        }
    }
    return std::nullopt;
}

void custom_prohelion() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float left_width = 220.0f;
    float right_width = avail.x - left_width - ImGui::GetStyle().ItemSpacing.x;

    // --- Vehicle/Motor/Slip Speed Widget ---
    auto messages = backend_get_messages();
    // Find keys for the signals
    std::optional<double> vehicle_velocity, motor_velocity, slip_speed;
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "Wavesculptor22")
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == "VehicleVelocity")
                vehicle_velocity = get_ws_value(messages, "VehicleVelocity");
            else if (s.name == "MotorVelocity")
                motor_velocity = get_ws_value(messages, "MotorVelocity");
            else if (s.name == "SlipSpeed")
                slip_speed = get_ws_value(messages, "SlipSpeed");
        }
    }

    ImGui::BeginChild("prohelion_left", ImVec2(left_width, 340), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Section header
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::Separator();
    ImGui::Spacing();

    // Draw a subtle background for the value area
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + left_width - ImGui::GetStyle().WindowPadding.x * 2, p0.y + 80);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // make this the same color as imgui buttons
	dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 225), 0, 0.8f);
    ImGui::PopFont();

    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::Text(" Vehicle Velocity:");
    ImGui::SameLine(120);
    if (vehicle_velocity)
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.0f), "%.2f m/s", *vehicle_velocity);
    else
        ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "--");

    ImGui::Text(" Motor Velocity:");
    ImGui::SameLine(120);
    if (motor_velocity)
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.0f), "%.2f rpm", *motor_velocity);
    else
        ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "--");

    ImGui::Text(" Slip Speed:");
    ImGui::SameLine(120);
    if (slip_speed)
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.0f), "%.2f Hz", *slip_speed);
    else
        ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "--");
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();

    auto bus_voltage = get_ws_value(messages, "BusVoltage");
    auto bus_current = get_ws_value(messages, "BusCurrent");

    // Display Bus Voltage
    ImGui::Text("Bus Voltage");
    if (bus_voltage)
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%.2f V", *bus_voltage);
    else
        ImGui::Text("--");

    // Display Bus Current
    ImGui::Text("Bus Current");
    if (bus_current)
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%.2f A", *bus_current);
    else
        ImGui::Text("--");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- Phase Current Plot ---
    ImGui::BeginChild("prohelion_phase_currents", ImVec2(right_width, 340), true);

    constexpr uint32_t PHASE_CURR_ID = 0x244;
    const char* dbc_name = "Wavesculptor22";
    const char* sig_b = "PhaseCurrentB";
    const char* sig_c = "PhaseCurrentC";

    std::string key_b = std::string(dbc_name) + ":" + std::to_string(PHASE_CURR_ID) + ":" + sig_b;
    std::string key_c = std::string(dbc_name) + ":" + std::to_string(PHASE_CURR_ID) + ":" + sig_c;

    auto itx_b = signal_times.find(key_b);
    auto ity_b = signal_values.find(key_b);
    auto itx_c = signal_times.find(key_c);
    auto ity_c = signal_values.find(key_c);

    ImVec2 plot_size = ImVec2(ImGui::GetContentRegionAvail().x, 300);

    // Determine time window for scrolling
    double t_min = 0.0, t_max = 0.0;
    double window = 10.0; // seconds to display

    // Use Phase B as reference for time axis
    if (itx_b != signal_times.end() && !itx_b->second.empty()) {
        t_max = itx_b->second.back();
        t_min = t_max - window;
        if (t_min < itx_b->second.front())
            t_min = itx_b->second.front();
    }

    if (ImPlot::BeginPlot("Phase Currents", plot_size)) {
        ImPlot::SetupAxes(
            "Time",
            "Current (A)",
            ImPlotAxisFlags_None,
            ImPlotAxisFlags_LockMin | ImPlotAxisFlags_LockMax
        );
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 400.0, ImPlotCond_Always);

        // Set up X axis to scroll with time
        if (t_max > t_min)
            ImPlot::SetupAxisLimits(ImAxis_X1, t_min, t_max, ImPlotCond_Always);

        if (itx_b != signal_times.end() &&
            ity_b != signal_values.end() &&
            !itx_b->second.empty() &&
            !ity_b->second.empty())
            ImPlot::PlotLine("Phase B", itx_b->second.data(), ity_b->second.data(),
                (int)itx_b->second.size());

        if (itx_c != signal_times.end() &&
            ity_c != signal_values.end() &&
            !itx_c->second.empty() &&
            !ity_c->second.empty())
            ImPlot::PlotLine("Phase C", itx_c->second.data(), ity_c->second.data(),
                (int)itx_c->second.size());

        // derived A only if both are present
        if (itx_b != signal_times.end() && ity_b != signal_values.end() &&
            itx_c != signal_times.end() && ity_c != signal_values.end() &&
            itx_b->second.size() == ity_b->second.size() &&
            itx_c->second.size() == ity_c->second.size())
        {
            size_t n = std::min(itx_b->second.size(), itx_c->second.size());
            std::vector<double> phase_a(n), times(n);
            for (size_t i = 0; i < n; ++i) {
                times[i] = itx_b->second[i];
                phase_a[i] = -(ity_b->second[i] + ity_c->second[i]);
            }
            ImPlot::PlotLine("Phase A*", times.data(), phase_a.data(), (int)n);
        }

        ImPlot::EndPlot();
    }

    ImGui::EndChild();

    // --- Below: Two tables for Limits and Errors ---
    ImGui::Spacing();

    ImVec2 below_size = ImGui::GetContentRegionAvail();
    float table_width = (below_size.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::BeginChild("prohelion_flags_tables", ImVec2(0, 250), true);

    ImGui::Columns(2, nullptr, false);

    // --- Limit Flags Table (Left) ---
    static const char* limit_signals[] = {
        "LimitReserved",
        "LimitIpmOrMotorTemp",
        "LimitBusVoltageLower",
        "LimitBusVoltageUpper",
        "LimitBusCurrent",
        "LimitVelocity",
        "LimitMotorCurrent",
        "LimitOutputVoltagePWM"
    };

    ImGui::BeginChild("limit_flags_table", ImVec2(table_width, 0), true);
    if (ImGui::BeginTable("LimitFlags", 2,ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Limit Flag");
        ImGui::TableSetupColumn("Triggered");
        ImGui::TableHeadersRow();
        for (const char* sig : limit_signals) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sig);
            ImGui::TableSetColumnIndex(1);
            auto v = get_ws_value(messages, sig);
            bool triggered = v && (*v != 0);
            ImU32 col = triggered ? IM_COL32(100, 255, 150, 255) : IM_COL32(255, 100, 100, 255);
            ImGui::TextColored(ImColor(col), triggered ? "ON" : "OFF");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // --- Error Flags Table (Right) ---
    static const char* error_signals[] = {
        "ErrorReserved",
        "ErrorMotorOverSpeed",
        "ErrorDesaturationFault",
        "Error15vRailUnderVoltage",
        "ErrorConfigRead",
        "ErrorWatchdogCausedLastReset",
        "ErrorBadMotorPositionHallSeq",
        "ErrorDcBusOverVoltage",
        "ErrorSoftwareOverCurrent",
        "ErrorHardwareOverCurrent"
    };

    ImGui::BeginChild("error_flags_table", ImVec2(table_width, 0), true);
    if (ImGui::BeginTable("ErrorFlags", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Error Flag");
        ImGui::TableSetupColumn("Triggered");
        ImGui::TableHeadersRow();
        for (const char* sig : error_signals) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sig);
            ImGui::TableSetColumnIndex(1);
            auto v = get_ws_value(messages, sig);
            bool triggered = v && (*v != 0);
            ImU32 col = triggered ? IM_COL32(100, 255, 150, 255) : IM_COL32(255, 100, 100, 255);
            ImGui::TextColored(ImColor(col), triggered ? "ON" : "OFF");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::EndChild();
}


void mppt_window(){
      auto messages = backend_get_messages();
      std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>> plots;

      for(const auto &mp : messages){
          const auto &msg = mp.second;
          if(msg.dbc_name != "MPPT")
              continue;
          for(const auto &sig : mp.second.signals){
              std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + sig.name;
              auto itx = signal_times.find(key);
              auto ity = signal_values.find(key);
              if(itx == signal_times.end() || ity == signal_values.end())
                  continue;
              if(itx->second.empty())
                  continue;
              std::string plot = g_plot_registry.get_plot("MPPT", mp.first, sig.name);
              if(plot.empty())
                  plot = sig.name;
              plots[plot].push_back({key, sig.name});
          }
      }
      render_plot_dock("MPPTDock", plots);
}

std::optional<double> get_mppt_value(const std::unordered_map<uint32_t, DbcMessage>& messages,
    int mppt_idx, const char* sig) {
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "MPPT")
            continue;
        if (static_cast<int>(mp.first >> 4) != mppt_idx)
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == sig) {
                std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                auto it = signal_values.find(key);
                if (it != signal_values.end() && !it->second.empty())
                    return it->second.back();
            }
        }
    }
    return std::nullopt;
}

static const char* mppt_mode_str(int v) {
    switch (v) {
    case 0: return "Const In Volt";
    case 1: return "Const In Curr";
    case 2: return "Min In Curr";
    case 3: return "Const Out Volt";
    case 4: return "Const Out Curr";
    case 5: return "Temp De-rate";
    case 6: return "Fault";
    default: return "Unknown";
    }
}


void custom_mppt() {
    auto messages = backend_get_messages();

    auto draw_light = [&](const char* label, std::optional<double> v) {
        bool on = v && *v;
        ImU32 col = on ? IM_COL32(100, 255, 150, 255) : IM_COL32(255, 100, 100, 255);
        float r = ImGui::GetTextLineHeight() * 0.4f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(r * 2, r * 2));
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + r, pos.y + r), r, col);
        ImGui::SameLine();
        ImGui::Text("%s", label);
        };
    auto draw_value = [&](const char* label, std::optional<double> v, const char* unit) {
        if (v) ImGui::Text("%s: %.2f %s", label, *v, unit);
        else ImGui::Text("%s: --", label);
        };

    ImGui::Columns(2, "mpptcols", false);
    for (int idx = 32; idx <= 33; ++idx) {
        auto enabled = get_mppt_value(messages, idx, "MPPT_Enabled");
        auto fault = get_mppt_value(messages, idx, "MPPT_Fault");
        auto mode = get_mppt_value(messages, idx, "MPPT_Mode");
        auto vin = get_mppt_value(messages, idx, "MPPT_Vin");
        auto iin = get_mppt_value(messages, idx, "MPPT_Iin");
        auto vout = get_mppt_value(messages, idx, "MPPT_Vout");
        auto iout = get_mppt_value(messages, idx, "MPPT_Iout");
        auto heatsink = get_mppt_value(messages, idx, "MPPT_HeatsinkTemperature");
        auto ambient = get_mppt_value(messages, idx, "MPPT_AmbientTemperature");

        ImGui::BeginChild(std::string("mppt_" + std::to_string(idx)).c_str(), ImVec2(0, 200), true);
        ImGui::Text("MPPT %d", idx);
        draw_light("Enabled", enabled);
        draw_light("Fault", fault && *fault);
        if (mode)
            ImGui::Text("Mode: %s", mppt_mode_str((int)*mode));
        else
            ImGui::Text("Mode: --");
        ImGui::Separator();
        draw_value("Vin", vin, "V");
        draw_value("Iin", iin, "A");
        draw_value("Vout", vout, "V");
        draw_value("Iout", iout, "A");
        draw_value("Heatsink", heatsink, "C");
        draw_value("Ambient", ambient, "C");
        ImGui::EndChild();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

void daq_window(){
      auto messages = backend_get_messages();
      std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>> plots;

      for(const auto &mp : messages){
          const auto &msg = mp.second;
          if(msg.dbc_name != "DAQ")
              continue;
          for(const auto &sig : mp.second.signals){
              std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + sig.name;
              auto itx = signal_times.find(key);
              auto ity = signal_values.find(key);
              if(itx == signal_times.end() || ity == signal_values.end())
                  continue;
              if(itx->second.empty())
                  continue;
              std::string plot = g_plot_registry.get_plot("DAQ", mp.first, sig.name);
              if(plot.empty())
                  plot = sig.name;
              plots[plot].push_back({key, sig.name});
          }
      }

      render_plot_dock("DAQDock", plots);
}

std::optional<double> get_daq_value(const std::unordered_map<uint32_t, DbcMessage>& messages,
    const char* sig) {
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "DAQ")
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == sig) {
                std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
                auto it = signal_values.find(key);
                if (it != signal_values.end() && !it->second.empty())
                    return it->second.back();
            }
        }
    }
    return std::nullopt;
}

static std::string get_daq_key(const std::unordered_map<uint32_t, DbcMessage>& messages,
    const char* sig) {
    for (const auto& mp : messages) {
        const auto& msg = mp.second;
        if (msg.dbc_name != "DAQ")
            continue;
        for (const auto& s : msg.signals) {
            if (s.name == sig)
                return msg.dbc_name + ":" + std::to_string(mp.first) + ":" + s.name;
        }
    }
    return "";
}

void custom_daq() {
    auto messages = backend_get_messages();

    auto bytes_tx = get_daq_value(messages, "Bytes_Transmited");
    auto tx_fail = get_daq_value(messages, "TX_Fail_Count");
    auto good_rx = get_daq_value(messages, "Good_Packet_Receive_Count");
    auto mac_fail = get_daq_value(messages, "MAC_ACK_Fail_Count");
    auto rf_rssi = get_daq_value(messages, "RSSI");
    auto lte_rssi = get_daq_value(messages, "LTE_RSSI");
    auto heartbeat = get_daq_value(messages, "Heartbeat");

    auto draw_value = [&](const char* label, std::optional<double> v) {
        if (v)
            ImGui::Text("%s: %.0f", label, *v);
        else
            ImGui::Text("%s: --", label);
        };

    ImGui::BeginChild("daq_status", ImVec2(0, 70), true);

    auto draw_heartbeat = [&](const char* label, std::optional<double> v) {
        bool on = v && *v;
        float t = static_cast<float>(ImGui::GetTime());
        float pulse = 0.5f + 0.5f * sinf(t * 4.0f);
        ImVec4 col = on ? ImVec4(0.4f, 1.0f, 0.6f, pulse) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        float radius = ImGui::GetTextLineHeight() * 0.4f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(radius * 2, radius * 2));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImGui::ColorConvertFloat4ToU32(col));
        ImGui::SameLine();
        ImGui::Text("%s", label);
        };

    draw_heartbeat("Server Heartbeat", heartbeat);
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("daq_tx", ImVec2(0, 110), true);
    ImGui::Text("RF Transmission Metrics");
    ImGui::Separator();
    draw_value("Bytes Tx", bytes_tx);
    draw_value("Good RX", good_rx);
    draw_value("TX Fail", tx_fail);
    draw_value("MAC ACK Fail", mac_fail);

    float good = good_rx.value_or(0);
    float bad = tx_fail.value_or(0) + mac_fail.value_or(0);
    if (good + bad > 0) {
        float ratio = good / (good + bad);
        char buf[32];
        snprintf(buf, sizeof(buf), "Success %.0f%%", ratio * 100.0f);
        ImGui::ProgressBar(ratio, ImVec2(-1, 0), buf);
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("daq_signal", ImVec2(0, 120), true);
    ImGui::Text("Signal Strength");
    ImGui::Separator();
    draw_value("RF RSSI", rf_rssi);
    draw_value("LTE RSSI", lte_rssi);

    ImGui::EndChild();

    ImGui::Spacing();

    // --- Admin Terminal (Unix-style) ---
    ImGui::Spacing();
    ImGui::BeginChild("admin_terminal", ImVec2(0, 200), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Terminal state (static for persistence)
    static std::vector<std::string> terminal_lines;
    static char terminal_input[256] = "";
    static bool scroll_to_bottom = false;

    // Terminal style
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 20, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 255, 180, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

    // Output area
    ImGui::BeginChild("terminal_output", ImVec2(0, 150), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (const std::string& line : terminal_lines) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (scroll_to_bottom)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Input area
    ImGui::PushItemWidth(-1);

    static bool clear_input = false;
    static bool remove_focus = false;
    if (ImGui::InputText("##terminal_input", terminal_input, sizeof(terminal_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (terminal_input[0] != '\0') {
            std::string cmd = terminal_input;
            terminal_lines.push_back("$ " + cmd);
            terminal_lines.push_back("echo: " + cmd);
            scroll_to_bottom = true;
            clear_input = true; // Defer clearing
            remove_focus = true; // Remove focus after enter
        }
    }
    // Only clear after input box is no longer active
    if (clear_input && !ImGui::IsItemActive()) {
        terminal_input[0] = '\0';
        clear_input = false;
    }
    // Remove focus after enter to prevent repeated submission
    if (remove_focus) {
        ImGui::SetKeyboardFocusHere(-1); // Move focus away from input
        remove_focus = false;
    }
    ImGui::PopItemWidth();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::EndChild();

}

void embededPlotContents(const char * dbc_name){
    int case_num = -1;
    auto it = dbc_idx.find(dbc_name);
    if(it != dbc_idx.end()){
        case_num = it->second;
    }
    switch(case_num){
    case 0: custom_bps(); break;
    case 1: custom_prohelion(); break;
 //   case 2: custom_mppt(); break;
  //  case 3: custom_controls(); break;
   // case 4: custom_daq(); break;
    default: return;
    }
}

void sigPlotContents(const char* dbc_name){
      auto messages = backend_get_messages();
      for(const auto &mp : messages){
          const auto &msg = mp.second;
          if(msg.dbc_name != dbc_name)
              continue;
          for(const auto &sig : mp.second.signals){
              std::string key = msg.dbc_name + ":" + std::to_string(mp.first) + ":" + sig.name;
              auto itx = signal_times.find(key);
              auto ity = signal_values.find(key);
              if(itx == signal_times.end() || ity == signal_values.end())
                  continue;
              const auto &xs = itx->second;
              const auto &ys = ity->second;
              if(xs.empty())
                  continue;
              if(ImPlot::BeginPlot(sig.name.c_str())){
                  std::cout << dbc_name << std::endl;
                  ImPlot::SetupAxes("Time", "Value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                  ImPlot::SetupAxisLimits(ImAxis_X1, xs.front(), xs.back(), ImGuiCond_Always);
                  ImPlot::PlotLine(sig.name.c_str(), xs.data(), ys.data(), ys.size());
                  ImPlot::EndPlot();
              }
          }
      }
  }

  void sourceConfigContents(){
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

      const char* protocol_list[] = { "Data Acq. Server", "Serial", "TCP" };
      static int protocol_idx = 0;

      static int connected = 0;
      connected = is_data_source_connected();

      // Check for update request from backend
      static bool no_update_now = false;
      if (!no_update_now) {
          if (read_update_avail()) {
              ImGui::OpenPopup("Update App");
          }
      }

      if (ImGui::BeginPopupModal("Update App", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("A new version of the application is available.\n\nWould you like to update now?");
          ImGui::Separator();

          if (ImGui::Button("Update Now", ImVec2(120, 0))) {
              ImGui::CloseCurrentPopup();
              perform_app_update(); // Calls the update logic as in your code
          }
          ImGui::SameLine();
          if (ImGui::Button("Later", ImVec2(120, 0))) {
              ImGui::CloseCurrentPopup();
              no_update_now = true;
          }
          ImGui::EndPopup();
      }

      ImGui::Combo("##01", &protocol_idx, protocol_list, ((int)sizeof(protocol_list) / sizeof(*(protocol_list))));
      ImGui::SameLine();
      if(ImGui::Button("Connect"))
        input_flag = 1;

      ImGui::SameLine();
      if(ImGui::Button("Close Connection"))
          close_flag = 1;

      ImGui::SameLine();
      if (ImGui::Button("Settings"))
          show_dbc_config = !show_dbc_config;

      ImGui::SameLine();
      if (ImGui::Button("Clear Table"))
          clear_can_store();

      if(protocol_idx == 0){
          ImVec2 slot_size(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
          ImGui::Dummy(slot_size);
          ImGui::Dummy(slot_size);
      }
      if(protocol_idx == 1){
        ImGui::InputTextWithHint("##02", serialHint.c_str(), serialBuf, sizeof(serialBuf));
        ImGui::InputTextWithHint("##03", baudHint.c_str(), baudBuf, sizeof(baudBuf), ImGuiInputTextFlags_CharsDecimal);
      }
      if(protocol_idx == 2){
        ImGui::InputTextWithHint("##04", ipHint.c_str(), ipBuf, sizeof(ipBuf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank);
        ImGui::InputTextWithHint("##05", portHint.c_str(), portBuf, sizeof(portBuf), ImGuiInputTextFlags_CharsDecimal);
      }
      
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      ImVec2 gradient_size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      ImVec2 p1 = ImVec2(p0.x + gradient_size.x, p0.y + gradient_size.y);
      ImU32 col_a;
      ImU32 col_b;
      if(!connected){
        col_a = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255)); // white
        col_b = ImGui::GetColorU32(IM_COL32(  0,   0,   0, 255)); // black
     } else {
        col_a = ImGui::GetColorU32(IM_COL32( 80, 150, 255, 255)); // light-blue
        col_b = ImGui::GetColorU32(IM_COL32(255, 100, 200, 255)); // pink
      }

      draw_list->AddRectFilledMultiColor(p0, p1, col_a, col_b, col_b, col_a);
      ImGui::InvisibleButton("##gradient2", gradient_size);

      if(close_flag == 1){
          kill_data_source();
          close_flag = 0;
          serialBuf[0] = baudBuf[0] = ipBuf[0] = portBuf[0] = '\0';
      }

      if(input_flag == 1){
          input_flag = 0;
          if(protocol_idx == 0){
              std::string ip = IP;
              std::string port = PORT;

             forward_tcp_source(ip, port);
             std::string portStr = "COM11";
             std::string baudStr = "115200";
            //forward_serial_source(portStr, baudStr);
          }
          if(protocol_idx == 1){
            std::string portStr(serialBuf);
            std::string baudStr(baudBuf);
            forward_serial_source(portStr, baudStr);
            serialHint = (!portStr.empty()) ? portStr : "e.g. /dev/ttyUSB0";
            baudHint   = (!baudStr.empty()) ? baudStr : "e.g. 115200";
          }
          if(protocol_idx == 2){
            std::string ipStr(ipBuf);
            std::string prtStr(portBuf);
            forward_tcp_source(ipStr, prtStr);
            ipHint   = (!ipStr.empty())  ? ipStr  : "e.g. 192.168.1.2";
            portHint = (!prtStr.empty()) ? prtStr : "e.g. 8080";
          }

          serialBuf[0] = baudBuf[0] = ipBuf[0] = portBuf[0] = '\0';
      }
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
    ImGui::SetNextWindowBgAlpha(0.10f); // Slight transparency
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

  void canTableContents() {
      //const char * path = "log.txt";
      //std::ofstream file(path, std::ios::out | std::ios::app);
      static ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter |
          ImGuiTableFlags_BordersV |
          ImGuiTableFlags_RowBg |
          ImGuiTableFlags_Resizable |
          ImGuiTableFlags_ScrollY;

      const CanStore& store = get_can_store();
      auto messages = backend_get_messages();
      float id_width = 96.0f;
      /*
      for (uint32_t id = 0; id < CanStore::MAX_IDS; ++id) {
          CanFrame frame;
          if (store.read(id, frame)) {
              auto mit = messages.find(id);
              std::string label;
              if (mit != messages.end()) label = mit->second.name;
              else {
                  char buf[8];
                  sprintf(buf, "0x%03X", id);
                  label = buf;
              }
              id_width = std::max(id_width, ImGui::CalcTextSize(label.c_str()).x);
          }
      }
      id_width += ImGui::GetStyle().CellPadding.x * 2.0f;
      */

      if (ImGui::BeginTable("cantable", 3, flags)) {
          ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, id_width);
          ImGui::TableSetupColumn("Len", ImGuiTableColumnFlags_WidthFixed, 24.0f);
          ImGui::TableSetupColumn("Decoded");
          ImGui::TableHeadersRow();

          for (uint32_t id = 0; id < CanStore::MAX_IDS; ++id) {
              CanFrame frame;
              if (store.read(id, frame)) {
                  ImGui::TableNextRow();
                  ImGui::TableSetColumnIndex(0);
                  auto mit = messages.find(id);
                  if (mit != messages.end())
                      ImGui::TextUnformatted(mit->second.name.c_str());
                  else
                      ImGui::Text("0x%03X", id);
                  ImGui::TableSetColumnIndex(1);
                  ImGui::Text("%d", frame.len);

                  ImGui::TableSetColumnIndex(2);
                  std::string decoded;
                  if (backend_decode_sep(id, frame, decoded, " | ")) {
                  //if(backend_decode(id, frame, decoded)){
                      ImGui::TextUnformatted(decoded.c_str());
                      //file << decoded.c_str() << std::endl;
                  }
                  else {
                      char buf[3 * 8 + 1] = { 0 };
                      for (uint8_t i = 0; i < frame.len; ++i)
                          sprintf(buf + i * 3, "%02X ", frame.data[i]);
                      ImGui::TextUnformatted(buf);
                  }
              }
          }
          ImGui::EndTable();
      }
      //file.close();
  }

  void CAN_TABLE(){
    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_Once);
      ImGui::Begin("CAN Data");
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
      canTableContents();
      ImGui::End();
  }

void modelWindowContents(){
    modelWindowPos = ImGui::GetWindowPos();
    modelWindowSize = ImGui::GetWindowSize();
    ImGui::SliderFloat3("Position", glm::value_ptr(uiSettings.modelPosition), -5.0f, 5.0f);

    ImGui::SliderFloat3("Rotation", glm::value_ptr(uiSettings.modelRotation), -180.0f, 180.0f);
    ImGui::SliderFloat3("Scale XYZ", glm::value_ptr(uiSettings.modelScale3D), 0.1f, 5.0f);

    ImGui::SliderFloat("Scale", &uiSettings.modelScale, 0.1f, 5.0f);
    ImGui::ColorEdit4("Effect", glm::value_ptr(uiSettings.effectColor));

    const char * effects[] = {"None", "Invert", "Grayscale", "Gradient"};
    ImGui::Combo("Effect Type", &uiSettings.effectType, effects, IM_ARRAYSIZE(effects));
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
  }

void dbcConfigContents(){
    if (ImGui::Button("Install Updates"))
        ImGui::OpenPopup("Update App");

    if (ImGui::BeginPopupModal("Update App", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to update the application?\nThe application will restart");
        if (ImGui::Button("Yes")) {
            perform_app_update();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

      ImGui::Text("Embedded DBC:");
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

      static char pathBuf[256] = "";
      ImGui::InputText("##File", pathBuf, sizeof(pathBuf));
      ImGui::SetItemTooltip("Relative path from the executable");
      ImGui::SameLine();
      if(ImGui::Button("Load")){
          std::string p(pathBuf);
          forward_dbc_load(p);
          pathBuf[0] = '\0';
      }

      ImGui::Text("Loaded DBC:");
      auto files = get_loaded_dbcs();
      for(size_t i = 0; i < files.size(); ++i){
          ImGui::TextUnformatted(files[i].c_str());
          ImGui::SameLine();
          std::string btn = "Unload##" + std::to_string(i);
          if(ImGui::Button(btn.c_str())){
              forward_dbc_unload(files[i]);
          }
      }
  }

void dbcConfigWindow(){
      ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Once);
      ImGui::Begin("DBC Config");
      dbcConfigContents();
      ImGui::End();
    }

void configTabContents(){
    // -- Top source config --
    ImGui::BeginChild("src_cfg", ImVec2(0, 120), true,
                      ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    sourceConfigContents();
    ImGui::EndChild();

    // grab total width before splitting
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("mid", ImVec2(avail.x, avail.y), false,
                      ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);

    if (show_dbc_config) {
        // 2 columns, no border
        ImGui::Columns(2, "cfgcols", false);
        // set column 0 to 75% of the total:
        ImGui::SetColumnWidth(0, avail.x * 0.75f);

        // column 0
        ImGui::BeginChild("cantab", ImVec2(0, 0), true);
        canTableContents();
        ImGui::EndChild();

        ImGui::NextColumn();

        // column 1
        ImGui::BeginChild("dbc", ImVec2(0, 0), true);
        dbcConfigContents();
        ImGui::EndChild();

        ImGui::Columns(1);
    }
    else {
        ImGui::BeginChild("cantab", ImVec2(0, 0), true);
        canTableContents();
        ImGui::EndChild();
    }

    ImGui::EndChild();
}

void drawConfigWindow(){
      ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Once);
      if(ImGui::Begin("Config Window")){
          configTabContents();
      }
      ImGui::End();
}

void imguiGrad(){
      auto dl = ImGui::GetWindowDrawList();
        ImVec2  p     = ImGui::GetWindowPos();
        ImVec2  sz    = ImGui::GetWindowSize();
        ImVec2  p_max = ImVec2(p.x + sz.x, p.y + sz.y);

        // fully opaque black at both top corners, fully opaque white at both bottom corners
        ImU32 col_top = IM_COL32(0, 0, 0, 255);
        ImU32 col_bot = IM_COL32(50, 50, 50, 255);

        dl->AddRectFilledMultiColor(
    p,       // upper‐left
    p_max,   // lower‐right
    col_top, // upper‐left corner
    col_top, // upper‐right corner
    col_bot, // lower‐right corner
    col_bot  // lower‐left corner
);
        /*
        for (int i = 0; i < (int)sz.y; ++i)
{
    float t = float(i) / sz.y;                   // 0.0 at top → 1.0 at bottom
    ImU32  c = IM_COL32(
        (int)(t * 255), (int)(t * 255), (int)(t * 255), 255
    );
    dl->AddRectFilled(
        ImVec2(p.x,      p.y + i),
        ImVec2(p.x + sz.x, p.y + i + 1),
        c
    );
}
*/

}

void drawMainWindow(){
      ImGuiViewport* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize(vp->WorkSize);
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | 
                               ImGuiWindowFlags_NoBringToFrontOnFocus;
      ImGui::Begin("Main", nullptr, flags);


      //imguiGrad();
      update_signal_data(); // this guy is heavy as fuck
      if(ImGui::BeginTabBar("maintabs")){

          if(ImGui::BeginTabItem("Config Window")){
              configTabContents();
              ImGui::EndTabItem();
          }

          /*
          if(ImGui::BeginTabItem("model")){
            modelWindowContents();
            ImGui::EndTabItem();
          } */

          auto loaded = get_loaded_dbcs();
          for(const auto &name : loaded){
              if(ImGui::BeginTabItem(name.c_str())){
                  sigPlotContents(name.c_str());
                  ImGui::EndTabItem();
              }
          }

          auto builtins = list_builtin_dbcs();
          size_t n = builtins.size();
          size_t i = 0;
          for (const auto& b : builtins) {
              if (i == n - 1) break; // Skip the final element
              if (!b.second) {
                  ++i;
                  continue;
              }
              if (ImGui::BeginTabItem(b.first.c_str())) {
                  embededPlotContents(b.first.c_str());
                  ImGui::EndTabItem();
              }
              ++i;
          }

          ImGui::EndTabBar();
      }
      ImGui::End();
}



  /*** Starts a new imGui frame and sets up windows and ui elements ***/
    // HERE HERE HERE
  void newFrame(VulkanExampleBase *example, bool updateFrameGraph) {
    // you gotta clean all this shit lmao
    ImGui::NewFrame();
//    drawMainWindow();
    ImPlot::ShowDemoWindow();
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

    pushConstBlock.gradTop = glm::vec4(1.00f, 1.00f, 1.00f, 1.00f);
    pushConstBlock.gradBottom = glm::vec4(1.00, 1.00f, 1.00f, 1.00f);

    pushConstBlock.invScreenSize = glm::vec2(1.0f / io.DisplaySize.x, 1.0f / io.DisplaySize.y);
    pushConstBlock.whitePixel = glm::vec2(io.Fonts->TexUvWhitePixel.x, io.Fonts->TexUvWhitePixel.y);
    pushConstBlock.u_time = (float)ImGui::GetTime();
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlock),
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
