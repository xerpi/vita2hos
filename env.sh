#!/bin/bash

set -e  # Exit on error

# Set environment variables
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
export DEVKITPRO=/opt/devkitpro
export PATH=$DEVKITPRO/tools/bin:$PATH

# Make sure submodules are initialized
git submodule update --init --recursive

# Clean any existing CMake files in root
rm -f CMakeCache.txt
rm -rf CMakeFiles
rm -f cmake_install.cmake
rm -f Makefile

# Create and enter build directory
rm -rf build
mkdir -p build
cd build

# Configure with CMake
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DVITASDK=$VITASDK \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
make -j$(nproc) VERBOSE=1
