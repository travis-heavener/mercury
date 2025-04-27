#!/bin/bash

# CD to user home
cd ~

# Clean existing
if [ -d "brotli-static" ]; then
    rm -rf brotli-static
fi

if [ -d "brotli-repo" ]; then
    rm -rf brotli-repo
fi

# Clone Brotli repo
git clone https://github.com/google/brotli ./brotli-repo
cd brotli-repo
mkdir build && cd build

# ==== Linux Build ====

# Prepare CMAKE for compilation
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBROTLI_BUNDLED_MODE=ON \
    -DBUILD_SHARED_LIBS=OFF

# Build
make

# Extract resources
mkdir ~/brotli-static
mkdir ~/brotli-static/lib

mv *.a ~/brotli-static/lib
ls -la
cp -r ~/brotli-repo/c/include ~/brotli-static/include

# ==== Windows Build ====

# Wipe build dir
cd ..
rm -rf build
mkdir build && cd build

# Set Windows cross-compiling with MinGW
cmake .. \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DBROTLI_BUNDLED_MODE=ON \
    -DBUILD_SHARED_LIBS=OFF

# Build
make

# Extract headers & linker files
mkdir ~/brotli-static/windows
mkdir ~/brotli-static/windows/lib

mv *.a ~/brotli-static/windows/lib
mv ~/brotli-repo/c/include ~/brotli-static/windows/include

# ==== Clean Up ====
cd ~
rm -rf ~/brotli-repo