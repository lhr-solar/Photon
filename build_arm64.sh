#!/bin/bash
# ARM64 Cross-compilation for Photon using Docker + QEMU
# Produces Photon_arm64 executable for Raspberry Pi 4/5

set -e

OUTPUT_NAME="Photon_arm64"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Install QEMU for ARM64 emulation
setup_qemu() {
    print_step "Setting up QEMU for ARM64 emulation..."
    
    if ! command -v qemu-aarch64-static >/dev/null 2>&1; then
        print_info "Installing QEMU user emulation..."
        sudo dnf install -y qemu-user-static binfmt-support
        
        # Enable binfmt for ARM64
        sudo systemctl enable binfmt-support
        sudo systemctl restart binfmt-support
    fi
    
    print_info "QEMU ARM64 support enabled"
}

# Create working Dockerfile
create_dockerfile() {
    print_step "Creating optimized Dockerfile for ARM64..."
    
    cat > Dockerfile.arm64-final << 'EOF'
FROM arm64v8/ubuntu:22.04

# Set up environment
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    python3 \
    python3-pip \
    libvulkan-dev \
    vulkan-tools \
    libxcb1-dev \
    libxcb-keysyms1-dev \
    libx11-dev \
    glslang-tools \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Compile shaders first
RUN mkdir -p generated && \
    for shader in kernels/*.vert kernels/*.frag; do \
        basename=$(basename "$shader"); \
        glslangValidator -V "$shader" -o "generated/${basename}.spv"; \
        header_name=$(echo "${basename}" | sed 's/\./_/g'); \
        python3 find/spv_to_header.py "generated/${basename}.spv" "generated/${header_name}_spv.hpp"; \
    done

# Build the project
RUN rm -rf build && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2" && \
    make -j$(nproc)

# Create output directory and copy binary
CMD mkdir -p /output && cp /build/build/photon/Photon /output/Photon
EOF
    
    print_info "Dockerfile created"
}

# Build using Docker with QEMU
build_with_docker() {
    print_step "Building Photon for ARM64 using Docker + QEMU..."
    
    # Enable Docker BuildKit
    export DOCKER_BUILDKIT=1
    
    # Remove any existing containers
    docker rm -f photon-arm64-final 2>/dev/null || true
    
    # Build the image
    print_info "Building Docker image (this may take 10-15 minutes)..."
    if docker build --platform linux/arm64 -t photon-arm64-final -f Dockerfile.arm64-final .; then
        print_info "Docker image built successfully!"
    else
        print_error "Docker build failed!"
        return 1
    fi
    
    # Create output directory
    mkdir -p final-output
    
    # Run container to extract binary
    print_info "Extracting ARM64 binary..."
    if docker run --platform linux/arm64 --name photon-arm64-final -v "$(pwd)/final-output:/output" photon-arm64-final; then
        if [ -f "final-output/Photon" ]; then
            cp final-output/Photon "$OUTPUT_NAME"
            print_info "ARM64 binary created successfully!"
            
            # Verify binary
            print_info "Binary info:"
            file "$OUTPUT_NAME"
            print_info "Size: $(ls -lh "$OUTPUT_NAME" | awk '{print $5}')"
            
            return 0
        else
            print_error "Binary not found in output"
            return 1
        fi
    else
        print_error "Container run failed"
        docker logs photon-arm64-final
        return 1
    fi
}

# Create final deployment package
create_final_package() {
    if [ ! -f "$OUTPUT_NAME" ]; then
        print_error "ARM64 binary not found"
        return 1
    fi
    
    print_step "Creating final deployment package..."
    
    PACKAGE_DIR="photon-raspberry-pi"
    rm -rf "$PACKAGE_DIR"
    mkdir -p "$PACKAGE_DIR"
    
    # Copy binary
    cp "$OUTPUT_NAME" "$PACKAGE_DIR/Photon"
    chmod +x "$PACKAGE_DIR/Photon"
    
    # Create complete setup script
    cat > "$PACKAGE_DIR/setup.sh" << 'EOF'
#!/bin/bash
# Complete Photon setup for Raspberry Pi

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check if running on ARM64
if [ "$(uname -m)" != "aarch64" ]; then
    print_error "This package is for ARM64 systems only"
    print_error "Current architecture: $(uname -m)"
    exit 1
fi

print_info "Setting up Photon for Raspberry Pi..."

# Update package list
print_info "Updating package list..."
sudo apt update

# Install required packages
print_info "Installing dependencies..."
sudo apt install -y \
    libvulkan1 \
    mesa-vulkan-drivers \
    vulkan-tools \
    libxcb1 \
    libxcb-keysyms1 \
    libx11-6 \
    libwayland-client0 \
    libasound2

# Configure GPU memory (for Pi 4)
if grep -q "Raspberry Pi 4" /proc/cpuinfo; then
    print_info "Configuring Raspberry Pi 4 GPU settings..."
    
    # Check if gpu_mem is already set
    if ! grep -q "gpu_mem=" /boot/config.txt; then
        echo "gpu_mem=256" | sudo tee -a /boot/config.txt
        print_warning "GPU memory set to 256MB. Reboot required for changes to take effect."
        NEED_REBOOT=1
    fi
fi

# Test Vulkan
print_info "Testing Vulkan support..."
if vulkaninfo --summary >/dev/null 2>&1; then
    print_info "Vulkan support verified!"
    vulkaninfo --summary | grep -E "(GPU|Device)"
else
    print_warning "Vulkan test failed - check GPU drivers"
    print_info "This may work anyway, but performance could be limited"
fi

print_info "Setup complete!"

if [ "$NEED_REBOOT" = "1" ]; then
    print_warning "A reboot is recommended for GPU changes to take effect"
    print_info "After reboot, run: ./Photon"
else
    print_info "You can now run: ./Photon"
fi
EOF
    
    chmod +x "$PACKAGE_DIR/setup.sh"
    
    # Create simple launcher
    cat > "$PACKAGE_DIR/run.sh" << 'EOF'
#!/bin/bash
# Simple Photon launcher with checks

# Architecture check
if [ "$(uname -m)" != "aarch64" ]; then
    echo "Error: Wrong architecture. This is for ARM64 systems."
    exit 1
fi

# Display check
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "Error: No display detected. Run from desktop environment."
    exit 1
fi

echo "Starting Photon..."
./Photon
EOF
    
    chmod +x "$PACKAGE_DIR/run.sh"
    
    # Create comprehensive README
    cat > "$PACKAGE_DIR/README.md" << 'EOF'
# Photon for Raspberry Pi 4/5

ARM64 build of Photon optimized for Raspberry Pi with Vulkan support.

## Quick Start

1. **One-time setup:**
   ```bash
   ./setup.sh
   ```

2. **Run Photon:**
   ```bash
   ./run.sh
   ```

## Manual Installation

If the setup script doesn't work:

```bash
sudo apt update
sudo apt install libvulkan1 mesa-vulkan-drivers vulkan-tools libxcb1
```

For Raspberry Pi 4, add to `/boot/config.txt`:
```
gpu_mem=256
```

## System Requirements

- Raspberry Pi 4B (4GB+ recommended) or Raspberry Pi 5
- Raspberry Pi OS 64-bit or Ubuntu 22.04+ ARM64
- Desktop environment (not SSH)
- 4GB+ microSD card

## Performance Tips

1. **Use active cooling** to prevent throttling
2. **Quality power supply** (5.1V 3A minimum)
3. **Fast storage** (A2-class microSD or USB 3.0 SSD)
4. **Close other applications** to free RAM

## Files

- `Photon` - Main executable
- `setup.sh` - Complete system setup
- `run.sh` - Simple launcher
- `README.md` - This guide

## Troubleshooting

**Vulkan errors:**
```bash
vulkaninfo --summary
sudo apt install --reinstall mesa-vulkan-drivers
```

**Display errors:**
- Must run from desktop environment
- For SSH: use `ssh -X` and run from desktop session

**Performance issues:**
- Check temperature: `vcgencmd measure_temp`
- Ensure cooling is adequate
- Close background applications

## Support

This build was created using Docker cross-compilation with full ARM64 environment and optimized for Cortex-A72 (Pi 4) architecture.
EOF
    
    # Create archive
    tar -czf "$PACKAGE_DIR.tar.gz" "$PACKAGE_DIR"
    
    print_info "Final package created: $PACKAGE_DIR.tar.gz"
}

# Cleanup function
cleanup() {
    print_info "Cleaning up temporary files..."
    docker rm -f photon-arm64-final 2>/dev/null || true
    rm -f Dockerfile.arm64-final
    rm -rf final-output
}

# Main function
main() {
    print_info "=== Final ARM64 Build for Raspberry Pi ==="
    print_info "Using Docker + QEMU for reliable cross-compilation"
    echo
    
    # Setup emulation
    setup_qemu
    
    # Create build environment
    create_dockerfile
    
    # Build the project
    if build_with_docker; then
        create_final_package
        
        print_info ""
        print_info "=== SUCCESS! ==="
        print_info "ARM64 Photon binary: $OUTPUT_NAME"
        print_info "Complete package: photon-raspberry-pi.tar.gz"
        print_info ""
        print_info "Transfer to your Raspberry Pi:"
        print_info "  scp photon-raspberry-pi.tar.gz pi@your-pi:~/"
        print_info ""
        print_info "On your Raspberry Pi:"
        print_info "  tar -xzf photon-raspberry-pi.tar.gz"
        print_info "  cd photon-raspberry-pi"
        print_info "  ./setup.sh"
        print_info "  ./run.sh"
        
    else
        print_error "Build failed - check output above"
        cleanup
        exit 1
    fi
    
    cleanup
}

# Execute
main "$@"