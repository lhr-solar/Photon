#include "gpuMetal.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

#include "../gui/titlebar.hpp"
#include "Inter_28pt_Regular_ttf.hpp"
#include "TablerIcons_ttf.hpp"
#include "imgui.h"
#include "imgui_impl_metal4.h"
#include "imgui_impl_sdl3.h"
#include "imnodes.h"
#include "implot.h"
#include "implot3d.h"

constexpr uint32_t kFramesInFlight = 3;

struct GPUMetalState {
  SDL_MetalView view = nullptr;
  CAMetalLayer* layer = nil;
  id<MTLDevice> device = nil;
  id<MTL4CommandQueue> commandQueue = nil;
  id<MTLSharedEvent> sharedEvent = nil;
  MTL4RenderPassDescriptor* renderPassDescriptor = nil;
  std::array<id<MTL4CommandAllocator>, kFramesInFlight> commandAllocators{};
  std::array<id<MTL4CommandBuffer>, kFramesInFlight> commandBuffers{};
  id<CAMetalDrawable> drawable = nil;
  id<MTL4RenderCommandEncoder> renderEncoder = nil;
};

namespace {
[[noreturn]] void fatalSdl(const char* message) {
  std::fprintf(stderr, "%s: %s\n", message, SDL_GetError());
  std::exit(1);
}

[[noreturn]] void fatalMetal(const char* message) {
  std::fprintf(stderr, "%s\n", message);
  std::exit(1);
}

bool titleBarHasInteractiveRect(const TitleBar* titleBar, int x, int y) {
  if (titleBar == nullptr) return false;
  const int count = std::clamp(titleBar->interactiveRectCount, 0, TitleBar::buttonCount);
  for (int i = 0; i < count; ++i) {
    const SDL_Rect& rect = titleBar->interactiveRects[i];
    if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) return true;
  }
  return false;
}

SDL_HitTestResult SDLCALL photonMetalWindowHitTest(SDL_Window* window, const SDL_Point* area,
                                                   void* data) {
  if ((window == nullptr) || (area == nullptr) || (data == nullptr)) return SDL_HITTEST_NORMAL;
  const auto* titleBar = static_cast<const TitleBar*>(data);
  if (!titleBar->enabled) return SDL_HITTEST_NORMAL;

  const Uint64 flags = SDL_GetWindowFlags(window);
  if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
    if ((area->y < titleBar->height) && !titleBarHasInteractiveRect(titleBar, area->x, area->y))
      return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
  }

  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window, &width, &height);

  constexpr int resizeBorder = 6;
  const bool left = area->x < resizeBorder;
  const bool right = area->x >= width - resizeBorder;
  const bool top = area->y < resizeBorder;
  const bool bottom = area->y >= height - resizeBorder;

  if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
  if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
  if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
  if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
  if (left) return SDL_HITTEST_RESIZE_LEFT;
  if (right) return SDL_HITTEST_RESIZE_RIGHT;
  if (top) return SDL_HITTEST_RESIZE_TOP;
  if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;

  if ((area->y < titleBar->height) && !titleBarHasInteractiveRect(titleBar, area->x, area->y))
    return SDL_HITTEST_DRAGGABLE;

  return SDL_HITTEST_NORMAL;
}
}  // namespace

void GPU::init() {
  @autoreleasepool {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) fatalSdl("SDL_Init failed");

    window = createWindow();
    if (window == nullptr) fatalSdl("SDL_CreateWindow failed");
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    metal = new GPUMetalState{};
    metal->device = MTLCreateSystemDefaultDevice();
    if (metal->device == nil) fatalMetal("Failed to create Metal device.");

    metal->view = SDL_Metal_CreateView(window);
    if (metal->view == nullptr) fatalSdl("SDL_Metal_CreateView failed");

    metal->layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metal->view);
    if (metal->layer == nil) fatalMetal("Failed to resolve CAMetalLayer from SDL_MetalView.");
    metal->layer.device = metal->device;
    metal->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metal->layer.opaque = NO;

    metal->commandQueue = [metal->device newMTL4CommandQueue];
    if (metal->commandQueue == nil) fatalMetal("Failed to create Metal 4 command queue.");
    metal->sharedEvent = [metal->device newSharedEvent];
    if (metal->sharedEvent == nil) fatalMetal("Failed to create Metal shared event.");
    metal->renderPassDescriptor = [MTL4RenderPassDescriptor new];
    if (metal->renderPassDescriptor == nil)
      fatalMetal("Failed to create Metal 4 render pass descriptor.");

    if (metal->layer.residencySet != nil)
      [metal->commandQueue addResidencySet:metal->layer.residencySet];

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
      metal->commandAllocators[i] = [metal->device newCommandAllocator];
      metal->commandBuffers[i] = [metal->device newCommandBuffer];
      if (metal->commandAllocators[i] == nil || metal->commandBuffers[i] == nil)
        fatalMetal("Failed to create Metal 4 frame command resources.");
    }

    swapchainImages.assign(kFramesInFlight, 0);
    resizeWindow();
  }
}

void GPU::imguiBackend(TitleBar* titleBar) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImPlot3D::CreateContext();
  ImNodes::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // The SDL3 backend reports Retina framebuffer scale on Apple. Keep that for rendering,
  // but do not scale fonts independently from Photon's fixed-size style metrics.
  io.ConfigDpiScaleFonts = false;
  io.IniFilename = "config.ini";

  ImFontConfig fontConfig;
  fontConfig.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF((void*)Inter_28pt_Regular_ttf,
                                 static_cast<int>(Inter_28pt_Regular_ttf_size), 28.0f, &fontConfig);
  static constexpr ImWchar tablerIconRanges[] = {
      0xEA00,
      0xFBFF,
      0,
  };
  ImFontConfig tablerFontConfig;
  tablerFontConfig.FontDataOwnedByAtlas = false;
  tablerFontConfig.PixelSnapH = false;
  tablerFontConfig.OversampleH = 2;
  tablerFontConfig.OversampleV = 2;
  io.Fonts->AddFontFromMemoryTTF((void*)TablerIcons_ttf, static_cast<int>(TablerIcons_ttf_size),
                                 28.0f, &tablerFontConfig, tablerIconRanges);
  for (float tierSize : {16.0f, 24.0f, 32.0f}) {
    io.Fonts->AddFontFromMemoryTTF((void*)Inter_28pt_Regular_ttf,
                                   static_cast<int>(Inter_28pt_Regular_ttf_size), tierSize,
                                   &fontConfig);
  }

  if (!ImGui_ImplMetal4_Init(metal->device, metal->commandQueue, kFramesInFlight))
    fatalMetal("Failed to initialize ImGui Metal 4 backend.");
  if (!ImGui_ImplSDL3_InitForMetal(window)) fatalSdl("Failed to initialize ImGui SDL3 backend");

  updateImguiDisplayMetrics();
  enableCustomTitlebar(titleBar);
}

void GPU::startFrame(uint32_t& imgIdx) {
  @autoreleasepool {
    frameActive = false;
    imgIdx = 0;
    if (metal == nullptr || window == nullptr) return;
    if (resizePending) resizeWindow();

    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
      SDL_Delay(10);
      return;
    }

    queryWindowPixelSize(width, height);
    if (width == 0 || height == 0) {
      resizePending = true;
      return;
    }

    frameInFlight = static_cast<uint32_t>(frameSerial % kFramesInFlight);
    imgIdx = frameInFlight;
    metal->layer.drawableSize = CGSizeMake(width, height);
    metal->drawable = [metal->layer nextDrawable];
    if (metal->drawable == nil) return;

    const uint64_t waitValue =
        (frameSerial >= kFramesInFlight) ? (frameSerial - kFramesInFlight + 1) : 0;
    [metal->sharedEvent waitUntilSignaledValue:waitValue timeoutMS:1000];

    MTLRenderPassColorAttachmentDescriptor* colorAttachment =
        metal->renderPassDescriptor.colorAttachments[0];
    colorAttachment.texture = metal->drawable.texture;
    colorAttachment.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    colorAttachment.loadAction = MTLLoadActionClear;
    colorAttachment.storeAction = MTLStoreActionStore;

    pendingSignalValue = frameSerial + 1;
    ImGui_ImplMetal4_NewFrame(metal->renderPassDescriptor, static_cast<int>(frameInFlight));
    frameActive = true;
  }
}

void GPU::startCommands() {
  if (!frameActive || metal == nullptr) return;
  @autoreleasepool {
    [metal->commandAllocators[frameInFlight] reset];
    [metal->commandBuffers[frameInFlight]
        beginCommandBufferWithAllocator:metal->commandAllocators[frameInFlight]];
    metal->renderEncoder = [metal->commandBuffers[frameInFlight]
        renderCommandEncoderWithDescriptor:metal->renderPassDescriptor];
    [metal->renderEncoder pushDebugGroup:@"Photon ImGui"];
  }
}

void GPU::imguiPresentation(const uint32_t) {
  if (!frameActive || metal == nullptr || metal->renderEncoder == nil) return;
  ImDrawData* drawData = ImGui::GetDrawData();
  ImGui_ImplMetal4_RenderDrawData(drawData, metal->commandBuffers[frameInFlight],
                                  metal->renderEncoder);
}

void GPU::endCommands() {
  if (!frameActive || metal == nullptr || metal->renderEncoder == nil) return;
  @autoreleasepool {
    [metal->renderEncoder popDebugGroup];
    [metal->renderEncoder endEncoding];
    [metal->commandBuffers[frameInFlight] endCommandBuffer];
  }
}

void GPU::submitFrame(const uint32_t) {
  if (!frameActive || metal == nullptr || metal->drawable == nil) return;
  @autoreleasepool {
    id<MTL4CommandBuffer> commandBuffers[] = {metal->commandBuffers[frameInFlight]};
    [metal->commandQueue waitForDrawable:metal->drawable];
    [metal->commandQueue commit:commandBuffers count:1];
    [metal->commandQueue signalEvent:metal->sharedEvent value:pendingSignalValue];
    [metal->commandQueue signalDrawable:metal->drawable];
    [metal->drawable present];

    frameSerial = pendingSignalValue;
    pendingSignalValue = 0;
    metal->renderEncoder = nil;
    metal->drawable = nil;
    frameActive = false;
  }
}

void GPU::resizeWindow() {
  uint32_t pixelWidth = 0;
  uint32_t pixelHeight = 0;
  queryWindowPixelSize(pixelWidth, pixelHeight);
  if (pixelWidth == 0 || pixelHeight == 0) {
    resizePending = true;
    return;
  }

  width = pixelWidth;
  height = pixelHeight;
  resizePending = false;
  if (metal != nullptr && metal->layer != nil)
    metal->layer.drawableSize = CGSizeMake(width, height);
  updateImguiDisplayMetrics();
}

void GPU::destroy() {
  @autoreleasepool {
    if (metal != nullptr && metal->sharedEvent != nil && frameSerial > 0)
      [metal->sharedEvent waitUntilSignaledValue:frameSerial timeoutMS:1000];

    if (ImGui::GetCurrentContext() != nullptr) {
      ImGui::DestroyPlatformWindows();
      ImGui_ImplMetal4_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImNodes::DestroyContext();
      ImPlot3D::DestroyContext();
      ImPlot::DestroyContext();
      ImGui::DestroyContext();
    }

    if (metal != nullptr) {
      if (metal->view != nullptr) SDL_Metal_DestroyView(metal->view);
      delete metal;
      metal = nullptr;
    }
    if (window != nullptr) {
      SDL_DestroyWindow(window);
      window = nullptr;
    }
    SDL_Quit();
  }
}

SDL_Window* GPU::createWindow() {
  SDL_PropertiesID properties = SDL_CreateProperties();
  if (properties != 0) {
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Photon");
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_MOUSE_GRABBED_BOOLEAN, false);
    SDL_Window* created = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (created != nullptr) return created;
  }

  return SDL_CreateWindow("Photon", width, height,
                          SDL_WINDOW_METAL | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS |
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
}

void GPU::enableCustomTitlebar(TitleBar* titleBarState) {
  if (titleBarState == nullptr || window == nullptr) return;
  titleBarState->window = window;
  SDL_SetWindowBordered(window, false);
  SDL_SetWindowHitTest(window, photonMetalWindowHitTest, titleBarState);
}

void GPU::updateImguiDisplayMetrics() {
  if (window == nullptr || ImGui::GetCurrentContext() == nullptr) return;
  int logicalWidth = 0;
  int logicalHeight = 0;
  SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
  uint32_t pixelWidth = 0;
  uint32_t pixelHeight = 0;
  queryWindowPixelSize(pixelWidth, pixelHeight);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(logicalWidth > 0 ? logicalWidth : width),
                          static_cast<float>(logicalHeight > 0 ? logicalHeight : height));
  io.DisplayFramebufferScale = ImVec2(
      logicalWidth > 0 ? static_cast<float>(pixelWidth) / static_cast<float>(logicalWidth) : 1.0f,
      logicalHeight > 0 ? static_cast<float>(pixelHeight) / static_cast<float>(logicalHeight)
                        : 1.0f);
}

void GPU::queryWindowPixelSize(uint32_t& outWidth, uint32_t& outHeight) const {
  outWidth = 0;
  outHeight = 0;
  if (window == nullptr) return;

  int drawableWidth = 0;
  int drawableHeight = 0;
  SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
  if ((drawableWidth > 0) && (drawableHeight > 0)) {
    outWidth = static_cast<uint32_t>(drawableWidth);
    outHeight = static_cast<uint32_t>(drawableHeight);
  }
}
