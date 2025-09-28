#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs
LIB_PATH=$(pwd)

# Update artifacts.lock
version="1.1.0"
if [ ! -e "artifacts.lock" ]; then
    touch artifacts.lock
    echo "" | gzip | base64 > artifacts.lock
fi

# Unpack artifacts
cat artifacts.lock | base64 --decode | gunzip > artifacts.raw

if grep -q "^brotli=" artifacts.raw; then
    sed -i "s/^brotli=.*$/brotli=$version/" artifacts.raw
else
    { echo "brotli=$version"; cat artifacts.raw; } > temp && mv temp artifacts.raw
fi

# Repack artifacts
cat artifacts.raw | gzip | base64 > artifacts.lock
rm -f artifacts.raw

# Clean existing
if [ -d "brotli" ]; then
    rm -rf brotli
fi

if [ -d "brotli-repo" ]; then
    rm -rf brotli-repo
fi

# Download Brotli tarball
wget -q --no-check-certificate https://github.com/google/brotli/archive/refs/tags/v$version.tar.gz -O brotli-repo.tar.gz
tar -xzf brotli-repo.tar.gz
echo "Fetched Brotli archive."

# Remove tarball & rename
rm -f brotli-repo.tar.gz
mv brotli-$version brotli-repo
cd brotli-repo

mkdir -p $LIB_PATH/brotli
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
mkdir $LIB_PATH/brotli/linux $LIB_PATH/brotli/linux/lib

mv *.a $LIB_PATH/brotli/linux/lib
cp -r $LIB_PATH/brotli-repo/c/include $LIB_PATH/brotli/linux/include

cd ..
# ==== Windows Build ====

# Wipe build dir
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

# ==== Clean Up ====
cd $LIB_PATH
rm -rf $LIB_PATH/brotli-repo

echo "✅ Successfully built Brotli v$version library."