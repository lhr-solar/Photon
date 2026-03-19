#pragma once
#include <iostream>
#include <thread>

#ifdef WIN
#ifdef NDEBUG 
    #define logs(x) do {} while(0)
#else 
inline void ensure_console() {
    static bool attached = false;
    if (!attached) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
        attached = true;
    }
}
#define logs(x) do { ensure_console(); std::cout << x << std::endl; } while(0)
#endif
#endif

#ifdef LINUX
#ifdef NDEBUG 
    #define logs(x) do {} while(0)
#else 
    #define logs(x){ const auto id = std::this_thread::get_id(); std::cout << "[0x" << std::hex << id << "] " << x << std::endl; }
#endif

#endif

#ifdef APPLE
#endif
