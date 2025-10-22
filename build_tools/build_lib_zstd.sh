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

if [ -d "zstd" ]; then
    read -r -p "This operation will overwrite an existing build of Zstandard. Continue? [y/N] " res
    res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
    if [[ "$res" =~ ^(yes|y)$ ]]; then
        rm -rf zstd
    else
        echo "Aborting..."
        exit 0
    fi
fi

# Fetch version
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^ZSTD=)(.*)$" )

# Clean existing
$TOOLS_PATH/safe_rm "zstd-$version"
$TOOLS_PATH/safe_rm "zstd-$version-linux"
$TOOLS_PATH/safe_rm "zstd-$version-windows"
$TOOLS_PATH/safe_rm "zstd-$version.tar.gz"

# Get Zstandard source
wget -q --no-check-certificate "https://github.com/facebook/zstd/releases/download/v$version/zstd-$version.tar.gz"
echo "Fetched Zstandard archive."

mkdir zstd
mkdir zstd/linux zstd/linux/include zstd/linux/lib
mkdir zstd/windows zstd/windows/include zstd/windows/lib

# Extract archive
tar -xzf "zstd-$version.tar.gz"
echo "Extracted archive."

# ==== Build in Parallel ====
cp -r "zstd-$version" "zstd-$version-windows"
mv "zstd-$version" "zstd-$version-linux"

(
    cd "zstd-$version-linux/lib"
    make -j$(nproc) libzstd.a 1> /dev/null
    cp *.h "$LIB_PATH/zstd/linux/include"
    mv libzstd.a "$LIB_PATH/zstd/linux/lib"
) &

(
    cd "zstd-$version-windows/lib"
    make -j$(nproc) libzstd.a CC=x86_64-w64-mingw32-gcc 1> /dev/null
    cp *.h "$LIB_PATH/zstd/windows/include"
    mv libzstd.a "$LIB_PATH/zstd/windows/lib"
) &

wait

# ==== Clean Up ====
cd "$LIB_PATH"
rm -rf "zstd-$version"
rm -rf "zstd-$version-linux"
rm -rf "zstd-$version-windows"
rm -f "zstd-$version.tar.gz"

# Update artifacts.lock
$TOOLS_PATH/update_artifact "zstd" "$version"

echo "✅ Successfully built Zstandard v$version library."