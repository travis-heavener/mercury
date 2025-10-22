#!/bin/bash

set -e

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

TOOLS_PATH=$(pwd)/build_tools/tools
cd libs
LIB_PATH=$(pwd)

if [ -d "zlib" ]; then
    read -r -p "This operation will overwrite an existing build of Zlib. Continue? [y/N] " res
    res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
    if [[ "$res" =~ ^(yes|y)$ ]]; then
        rm -rf zlib
    else
        echo "Aborting..."
        exit 0
    fi
fi

# Fetch version
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^ZLIB=)(.*)$" )

# Clean existing
$TOOLS_PATH/safe_rm "zlib-$version"
$TOOLS_PATH/safe_rm "zlib-$version-linux"
$TOOLS_PATH/safe_rm "zlib-$version-windows"
$TOOLS_PATH/safe_rm "zlib-$version.tar.gz"

# Get Zlib source
wget -q --no-check-certificate "https://zlib.net/zlib-$version.tar.gz"
echo "Fetched Zlib archive."

mkdir zlib
mkdir zlib/linux zlib/linux/include zlib/linux/lib
mkdir zlib/windows zlib/windows/include zlib/windows/lib

# Extract archive
tar -xzf "zlib-$version.tar.gz"
echo "Extracted archive."

# ==== Build in Parallel ====
cp -r "zlib-$version" "zlib-$version-windows"
mv "zlib-$version" "zlib-$version-linux"

(
    cd "zlib-$version-linux"

    cp zlib.h "$LIB_PATH/zlib/linux/include/"
    cp zconf.h "$LIB_PATH/zlib/linux/include/"

    ./configure 1> /dev/null
    make -j$(nproc) libz.a
    mv libz.a "$LIB_PATH/zlib/linux/lib/"
) &

(
    cd "zlib-$version-windows"

    MAKEFILE_PATH="win32/Makefile.gcc"
    sed -i 's/^PREFIX *=.*$/PREFIX=x86_64-w64-mingw32-/' "$MAKEFILE_PATH"
    sed -i 's/^CC *=.*$/CC=\$(PREFIX)gcc-win32/' "$MAKEFILE_PATH"

    make -j$(nproc) -B -f "$MAKEFILE_PATH" \
        BINARY_PATH="/dev/null" \
        INCLUDE_PATH="$LIB_PATH/zlib/windows/include" \
        LIBRARY_PATH="$LIB_PATH/zlib/windows/lib" 1> /dev/null

    make -j$(nproc) -B -f "$MAKEFILE_PATH" install \
        BINARY_PATH="/dev/null" \
        INCLUDE_PATH="$LIB_PATH/zlib/windows/include" \
        LIBRARY_PATH="$LIB_PATH/zlib/windows/lib" 1> /dev/null

    # Remove extra pkgconfig
    rm -rf "$LIB_PATH/zlib/windows/lib/pkgconfig"
) &

wait

# ==== Clean Up ====
cd "$LIB_PATH"
rm -rf "zlib-$version"
rm -rf "zlib-$version-linux"
rm -rf "zlib-$version-windows"
rm -f "zlib-$version.tar.gz"

# Update artifacts.lock
$TOOLS_PATH/update_artifact "zlib" "$version"

echo "✅ Successfully built Zlib v$version library."