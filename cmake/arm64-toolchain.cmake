set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compilation toolchain
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Prevent CMake from testing the compilers
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Sysroot (use Fedora's if available)
if(EXISTS "/usr/aarch64-redhat-linux/sys-root/fc42")
    set(CMAKE_SYSROOT "/usr/aarch64-redhat-linux/sys-root/fc42")
    set(CMAKE_FIND_ROOT_PATH "/usr/aarch64-redhat-linux/sys-root/fc42")
endif()

# Compiler flags for ARM64 optimization
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a72 -O2 -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a72 -O2 -fPIC")

# Search paths - only search in target environment for libraries and includes
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY) 
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Force static linking for problematic libraries
set(BUILD_SHARED_LIBS OFF)

# Set library search paths for cross-compilation
set(CMAKE_LIBRARY_PATH 
    "${CMAKE_SYSROOT}/usr/lib64"
    "${CMAKE_SYSROOT}/usr/lib"
    "${CMAKE_SYSROOT}/lib64" 
    "${CMAKE_SYSROOT}/lib"
)
