#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "static_libs" ]; then
    mkdir static_libs
fi

cd static_libs
LIB_PATH=$(pwd)

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
mkdir -p $LIB_PATH/brotli
mkdir build && cd build

# ==== Linux Build ====

if [ -z "$1" ] || [ "$1" == "linux" ]; then
    # Prepare CMAKE for compilation
    cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBROTLI_BUNDLED_MODE=ON \
    -DBUILD_SHARED_LIBS=OFF

    # Build
    make

    # Extract resources
    mkdir $LIB_PATH/brotli/linux $LIB_PATH/brotli/linux/lib

    mv *.a $LIB_PATH/brotli/linux/lib
    cp -r $LIB_PATH/brotli-repo/c/include $LIB_PATH/brotli/linux/include
fi

cd ..
# ==== Windows Build ====

# Wipe build dir
if [ -z "$1" ] || [ "$1" == "windows" ]; then
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
    mkdir $LIB_PATH/brotli/windows $LIB_PATH/brotli/windows/lib

    mv *.a $LIB_PATH/brotli/windows/lib
    mv $LIB_PATH/brotli-repo/c/include $LIB_PATH/brotli/windows/include
fi

# ==== Clean Up ====
cd $LIB_PATH
rm -rf $LIB_PATH/brotli-repo

echo "✅ Successfully built static Brotli binaries."