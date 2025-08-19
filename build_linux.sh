#!/bin/bash

# Build script for Linux platform

echo "Building Calaos Remote UI for Linux..."

# Create build directory
mkdir -p build_linux
cd build_linux

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

echo "Build complete. Executable: build_linux/calaos-remote-ui"