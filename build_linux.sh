#!/bin/bash

# Build script for Calaos Remote UI Linux
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Building Calaos Remote UI for Linux${NC}"

# Create build directory
BUILD_DIR="build_linux"
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
echo -e "${YELLOW}Building...${NC}"
make -j$(nproc)

# Check if build succeeded
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build completed successfully!${NC}"
    echo -e "${BLUE}Executable: $(pwd)/calaos-remote-ui${NC}"
    
    # Show available backends
    echo -e "${YELLOW}Available display backends:${NC}"
    ./calaos-remote-ui --list-backends
    
    echo -e "${YELLOW}Usage examples:${NC}"
    echo "  ./calaos-remote-ui"
    echo "  ./calaos-remote-ui --display-backend sdl"
    echo "  ./calaos-remote-ui --input-backend libinput"
    echo "  ./calaos-remote-ui --display-backend x11 --input-backend evdev"
    echo "  CALAOS_DISPLAY_BACKEND=x11 ./calaos-remote-ui"
    echo "  CALAOS_INPUT_BACKEND=libinput ./calaos-remote-ui"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi