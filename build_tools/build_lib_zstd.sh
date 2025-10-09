#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs
LIB_PATH=$(pwd)

# Update artifacts.lock
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^ZSTD=)(.*)$" )
if [ ! -e "artifacts.lock" ]; then
    touch artifacts.lock
    echo "" | gzip | base64 > artifacts.lock
fi

# Unpack artifacts
cat artifacts.lock | base64 --decode | gunzip > artifacts.raw

if grep -q "^zstd=" artifacts.raw; then
    sed -i "s/^zstd=.*$/zstd=$version/" artifacts.raw
else
    { echo "zstd=$version"; cat artifacts.raw; } > temp && mv temp artifacts.raw
fi

# Repack artifacts
cat artifacts.raw | gzip | base64 > artifacts.lock
rm -f artifacts.raw

# Clean existing
if [ -d "zstd" ]; then
    rm -rf zstd
fi

if [ -d "zstd-$version" ]; then
    rm -rf "zstd-$version"
fi

if [ -f "zstd-$version.tar.gz" ]; then
    rm -f "zstd-$version.tar.gz"
fi

# Get Zstandard source
wget -q --no-check-certificate "https://github.com/facebook/zstd/releases/download/v$version/zstd-$version.tar.gz"
echo "Fetched Zstandard archive."

mkdir zstd
mkdir zstd/linux zstd/linux/include zstd/linux/lib
mkdir zstd/windows zstd/windows/include zstd/windows/lib

# Unpack tarball
tar -xzf "zstd-$version.tar.gz"
echo "Extracted archive."
cd "zstd-$version/lib"

# ==== Linux Build ====

# Make libzstd.a
make libzstd.a

# Copy include headers & static lib
cp *.h "$LIB_PATH/zstd/linux/include"
mv libzstd.a "$LIB_PATH/zstd/linux/lib"

echo "Built Linux binaries."

# ==== Windows Build ====

# Make libzstd.a
make libzstd.a CC=x86_64-w64-mingw32-gcc

# Copy include headers & static lib
cp *.h "$LIB_PATH/zstd/windows/include"
mv libzstd.a "$LIB_PATH/zstd/windows/lib"

echo "Built Windows binaries."

# ==== Clean Up ====

cd ../..
rm -rf "zstd-$version"

rm -f "$LIB_PATH/zstd-$version.tar.gz"

echo "✅ Successfully built Zstandard v$version library."