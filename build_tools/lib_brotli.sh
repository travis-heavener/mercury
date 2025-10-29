#!/bin/bash

set -e

# CD into project directory
cd "$(dirname "$0")/../"

lockfile="/tmp/libs.lock"
exec 200>"$lockfile"
flock -x 200
if [ ! -d "libs" ]; then
    mkdir libs
fi
flock -u 200

TOOLS_PATH=$(pwd)/build_tools/tools
cd libs
LIB_PATH=$(pwd)

# Clean existing
if [ -d "brotli" ]; then
    if [ -t 0 ]; then
        read -r -p "This operation will overwrite an existing build of Brotli. Continue? [y/N] " res
        res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
        if [[ "$res" =~ ^(yes|y)$ ]]; then
            rm -rf brotli
        else
            echo "Aborting..."
            exit 0
        fi
    else
        # No shell connected
        rm -rf brotli
    fi
fi

# Fetch version
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^BROTLI=)(.*)$" )

# Clean existing
$TOOLS_PATH/safe_rm "brotli-$version.tar.gz"
$TOOLS_PATH/safe_rm "brotli-$version"
$TOOLS_PATH/safe_rm "brotli-$version-linux"
$TOOLS_PATH/safe_rm "brotli-$version-windows"

# Download Brotli tarball
wget -q --no-check-certificate "https://github.com/google/brotli/archive/refs/tags/v$version.tar.gz" -O "brotli-$version.tar.gz"
echo "Fetched Brotli archive."

mkdir brotli
mkdir brotli/linux brotli/linux/include brotli/linux/lib
mkdir brotli/windows brotli/windows/include brotli/windows/lib

# Extract archive
tar -xzf "brotli-$version.tar.gz"
echo "Extracted archive."

# ==== Build in Parallel ====
cp -r "brotli-$version" "brotli-$version-windows"
mv "brotli-$version" "brotli-$version-linux"

(
    cd "brotli-$version-linux"
    mkdir build && cd build

    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBROTLI_BUNDLED_MODE=ON \
        -DBUILD_SHARED_LIBS=OFF &> /dev/null
    make -j$(nproc) &> /dev/null

    # Move library files
    mv *.a "$LIB_PATH/brotli/linux/lib"
    cp -r ../c/include "$LIB_PATH/brotli/linux"
) &

(
    cd "brotli-$version-windows"
    mkdir build && cd build

    cmake .. \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
        -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
        -DCMAKE_BUILD_TYPE=Release \
        -DBROTLI_BUNDLED_MODE=ON \
        -DBUILD_SHARED_LIBS=OFF &> /dev/null
    make -j$(nproc) &> /dev/null

    # Move library files
    mv *.a "$LIB_PATH/brotli/windows/lib"
    cp -r ../c/include "$LIB_PATH/brotli/windows"
) &

wait

# ==== Clean Up ====
cd "$LIB_PATH"
rm -f "brotli-$version.tar.gz"
rm -rf "brotli-$version-linux"
rm -rf "brotli-$version-windows"

# Update artifacts.lock
$TOOLS_PATH/update_artifact "brotli" "$version"

echo "✅ Successfully built Brotli v$version library."