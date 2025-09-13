# ARM64 Cross-Compilation for Raspberry Pi

## Quick Build

To create `Photon_arm64` executable for Raspberry Pi:

```bash
./build_arm64.sh
```

This will create:
- `Photon_arm64` - ARM64 executable
- `photon-raspberry-pi.tar.gz` - Complete deployment package

## Requirements

- Docker with QEMU support
- Internet connection (first build downloads ~200MB)
- ~15 minutes build time

## Transfer to Raspberry Pi

```bash
scp photon-raspberry-pi.tar.gz pi@your-pi:~/
```

On Raspberry Pi:
```bash
tar -xzf photon-raspberry-pi.tar.gz
cd photon-raspberry-pi
./setup.sh    # Install dependencies once
./run.sh      # Launch Photon
```

## Files Modified for Cross-Compilation

- `CMakeLists.txt` - Added cross-compilation support
- `photon/engine/photon.cpp` - C++17 compatibility fix
- `photon/gpu/CMakeLists.txt` - Conditional kernel dependencies
- `photon/gui/CMakeLists.txt` - Conditional kernel dependencies
- `cmake/arm64-toolchain.cmake` - Cross-compilation toolchain