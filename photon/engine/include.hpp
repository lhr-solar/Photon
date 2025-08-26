/*photon headers*/
#pragma once

#include <iostream>
#ifdef NDEBUG
#define log(x) do {} while(0)
#else
#define log(x) std::cout << x << '\n'
#endif

static inline unsigned rdtsc(){
    unsigned x;
    __asm__ __volatile__ ( "rdtsc" : "=a"(x) : : "edx" );
    return x;
}

#include <stdlib.h>
[[noreturn]] inline void fatal(const std::string& msg, int32_t code) { std::cerr << msg << " " << std::endl; std::exit(code); }
