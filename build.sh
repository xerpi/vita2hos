#!/bin/bash

# Set environment variables
export DEVKITPRO=/opt/devkitpro
export PATH=$DEVKITPRO/tools/bin:$PATH

# Create and enter build directory
rm -rf build
mkdir build
cd build

# Configure with CMake using Unix Makefiles generator
cmake -G "Unix Makefiles" \
      -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -B . \
      -S .. \
      --debug-output

# Build
make VERBOSE=1
