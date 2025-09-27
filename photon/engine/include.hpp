/*photon headers*/
#pragma once

#include <iostream>

#if defined(_WIN32)
#define WIN
#endif

#if defined(VK_USE_PLATFORM_XCB_KHR)
#define XCB
#endif

#if defined WIN
#include <windows.h>
#include <fcntl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <io.h>
#include <ShellScalingAPI.h>
#endif

#ifdef NDEBUG
    #define logs(x) do {} while(0)
#elif defined(XCB)
    #define logs(x) std::cout << x << '\n'
#elif defined(WIN)
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

#ifdef XCB
static inline unsigned rdtsc(){
    unsigned x;
    __asm__ __volatile__ ( "rdtsc" : "=a"(x) : : "edx" );
    return x;
}
#endif

#include <stdlib.h>
[[noreturn]] inline void fatal(const std::string& msg, int32_t code) { std::cerr << msg << " " << std::endl; std::exit(code); }
