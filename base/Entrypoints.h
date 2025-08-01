#if defined(_WIN32)
/*
 * Windows
 */
#define PHOTON_MAIN()                                                        \
  /* a single global pointer, initialized to null */                         \
  static Photon* photon = nullptr;                                           \
                                                                              \
  /* your Win32 message pump callback */                                     \
  LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) { \
    if (photon)                                                               \
      photon->handleMessages(hWnd, uMsg, wParam, lParam);                     \
    return DefWindowProc(hWnd, uMsg, wParam, lParam);                         \
  }                                                                           \
                                                                              \
  /* the real entry point */                                                  \
  int APIENTRY WinMain(                                                       \
      _In_ HINSTANCE hInstance,                                               \
      _In_opt_ HINSTANCE hPrevInstance,                                       \
      _In_ LPSTR     /*lpCmdLine*/,                                           \
      _In_ int       /*nCmdShow*/) {                                          \
    /* forward the CRT args into your photon args vector */                   \
    for (int i = 0; i < __argc; ++i)                                          \
      Photon::args.push_back(__argv[i]);                                      \
                                                                              \
    photon = new Photon();                                                    \
    photon->initVulkan();                                                     \
    photon->setupWindow(hInstance, WndProc);                                  \
    photon->prepare();                                                        \
    photon->renderLoop();                                                     \
    delete photon;                                                            \
    return 0;                                                                 \
  }

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
/*
 * Android
 */
#define PHOTON_MAIN()                                                          \
  VulkanExample *vulkanExample;                                                \
  void android_main(android_app *state) {                                      \
    vulkanExample = new VulkanExample();                                       \
    state->userData = vulkanExample;                                           \
    state->onAppCmd = VulkanExample::handleAppCommand;                         \
    state->onInputEvent = VulkanExample::handleAppInput;                       \
    androidApp = state;                                                        \
    vks::android::getDeviceConfig();                                           \
    vulkanExample->renderLoop();                                               \
    delete (vulkanExample);                                                    \
  }

#elif defined(_DIRECT2DISPLAY)
/*
 * Direct-to-display
 */
#define PHOTON_MAIN()                                                          \
  VulkanExample *vulkanExample;                                                \
  static void handleEvent() {}                                                 \
  int main(const int argc, const char *argv[]) {                               \
    for (size_t i = 0; i < argc; i++) {                                        \
      VulkanExample::args.push_back(argv[i]);                                  \
    };                                                                         \
    vulkanExample = new VulkanExample();                                       \
    vulkanExample->initVulkan();                                               \
    vulkanExample->prepare();                                                  \
    vulkanExample->renderLoop();                                               \
    delete (vulkanExample);                                                    \
    return 0;                                                                  \
  }

#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
/*
 * Direct FB
 */
#define PHOTON_MAIN()                                                          \
  VulkanExample *vulkanExample;                                                \
  static void handleEvent(const DFBWindowEvent *event) {                       \
    if (vulkanExample != NULL) {                                               \
      vulkanExample->handleEvent(event);                                       \
    }                                                                          \
  }                                                                            \
  int main(const int argc, const char *argv[]) {                               \
    for (size_t i = 0; i < argc; i++) {                                        \
      VulkanExample::args.push_back(argv[i]);                                  \
    };                                                                         \
    vulkanExample = new VulkanExample();                                       \
    vulkanExample->initVulkan();                                               \
    vulkanExample->setupWindow();                                              \
    vulkanExample->prepare();                                                  \
    vulkanExample->renderLoop();                                               \
    delete (vulkanExample);                                                    \
    return 0;                                                                  \
  }

#elif (defined(VK_USE_PLATFORM_WAYLAND_KHR) ||                                 \
       defined(VK_USE_PLATFORM_HEADLESS_EXT))
/*
 * Wayland / headless
 */
#define PHOTON_MAIN()                                                          \
  Photon *photon;                                                \
  int main(const int argc, const char *argv[]) {                               \
    for (size_t i = 0; i < argc; i++) {                                        \
      Photon::args.push_back(argv[i]);                                  \
    };                                                                         \
    photon = new Photon();                                       \
    photon->initVulkan();                                               \
    photon->setupWindow();                                              \
    photon->prepare();                                                  \
    photon->renderLoop();                                               \
    delete (photon);                                                    \
    return 0;                                                                  \
  }

#elif defined(VK_USE_PLATFORM_XCB_KHR)
/*
 * X11 Xcb
 */
#define PHOTON_MAIN()                                                          \
  Photon *photon;                                                              \
  static void handleEvent(const xcb_generic_event_t *event) {                  \
    if (photon != NULL) {                                                      \
      photon->handleEvent(event);                                              \
    }                                                                          \
  }                                                                            \
  int main(const int argc, const char *argv[]) {                               \
    for (size_t i = 0; i < argc; i++) {                                        \
      Photon::args.push_back(argv[i]);                                         \
    };                                                                         \
    photon = new Photon();                                                     \
    photon->initVulkan();                                                      \
    photon->setupWindow();                                                     \
    photon->prepare();                                                         \
    photon->renderLoop();                                                      \
    delete (photon);                                                           \
    return 0;                                                                  \
  }

#elif (defined(VK_USE_PLATFORM_IOS_MVK) ||                                     \
       defined(VK_USE_PLATFORM_MACOS_MVK) ||                                   \
       defined(VK_USE_PLATFORM_METAL_EXT))
/*
 * iOS and macOS (using MoltenVK)
 */
#if defined(VK_EXAMPLE_XCODE_GENERATED)
#define PHOTON_MAIN()                                                          \
  VulkanExample *vulkanExample;                                                \
  int main(const int argc, const char *argv[]) {                               \
    @autoreleasepool {                                                         \
      for (size_t i = 0; i < argc; i++) {                                      \
        VulkanExample::args.push_back(argv[i]);                                \
      };                                                                       \
      vulkanExample = new VulkanExample();                                     \
      vulkanExample->initVulkan();                                             \
      vulkanExample->setupWindow(nullptr);                                     \
      vulkanExample->prepare();                                                \
      vulkanExample->renderLoop();                                             \
      delete (vulkanExample);                                                  \
    }                                                                          \
    return 0;                                                                  \
  }
#else
#define PHOTON_MAIN()
#endif

#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
/*
 * QNX Screen
 */
#define PHOTON_MAIN()                                                          \
  VulkanExample *vulkanExample;                                                \
  int main(const int argc, const char *argv[]) {                               \
    for (int i = 0; i < argc; i++) {                                           \
      VulkanExample::args.push_back(argv[i]);                                  \
    };                                                                         \
    vulkanExample = new VulkanExample();                                       \
    vulkanExample->initVulkan();                                               \
    vulkanExample->setupWindow();                                              \
    vulkanExample->prepare();                                                  \
    vulkanExample->renderLoop();                                               \
    delete (vulkanExample);                                                    \
    return 0;                                                                  \
  }
#endif
