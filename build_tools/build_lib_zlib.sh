#!/bin/bash

set -e

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

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

# Update artifacts.lock
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^ZLIB=)(.*)$" )
if [ ! -e "artifacts.lock" ]; then
    touch artifacts.lock
    echo "" | gzip | base64 > artifacts.lock
fi

# Unpack artifacts
cat artifacts.lock | base64 --decode | gunzip > artifacts.raw

if grep -q "^zlib=" artifacts.raw; then
    sed -i "s/^zlib=.*$/zlib=$version/" artifacts.raw
else
    { echo "zlib=$version"; cat artifacts.raw; } > temp && mv temp artifacts.raw
fi

# Repack artifacts
cat artifacts.raw | gzip | base64 > artifacts.lock
rm -f artifacts.raw

# Clean existing
if [ -d "zlib-$version" ]; then
    rm -rf "zlib-$version"
fi

if [ -f "zlib-$version.tar.gz" ]; then
    rm -f "zlib-$version.tar.gz"
fi

# Get Zlib source
wget -q --no-check-certificate "https://zlib.net/zlib-$version.tar.gz"
echo "Fetched Zlib archive."

mkdir zlib
mkdir zlib/linux zlib/linux/include zlib/linux/lib
mkdir zlib/windows zlib/windows/include zlib/windows/lib

# ==== Linux Build ====

# Unpack tarball
tar -xzf "zlib-$version.tar.gz"
cd "zlib-$version"

# Copy headers
cp zlib.h "$LIB_PATH/zlib/linux/include/"
cp zconf.h "$LIB_PATH/zlib/linux/include/"

# Static build
./configure 1> /dev/null
make libz.a
mv libz.a "$LIB_PATH/zlib/linux/lib/"

# Reset for Windows build
cd ../
rm -rf "zlib-$version"

# ==== Window Build ====

# Unpack tarball
tar -xzf "zlib-$version.tar.gz"
cd "zlib-$version"

# Copy headers
# cp *.h "$LIB_PATH/zlib/windows/include/"

# Static build
MAKEFILE_PATH="win32/Makefile.gcc"
sed -i 's/^PREFIX *=.*$/PREFIX=x86_64-w64-mingw32-/' "$MAKEFILE_PATH"
sed -i 's/^CC *=.*$/CC=\$(PREFIX)gcc-win32/' "$MAKEFILE_PATH"

make -B -f "$MAKEFILE_PATH" \
    BINARY_PATH="/dev/null" \
    INCLUDE_PATH="$LIB_PATH/zlib/windows/include" \
    LIBRARY_PATH="$LIB_PATH/zlib/windows/lib" 1> /dev/null

make -B -f "$MAKEFILE_PATH" install \
    BINARY_PATH="/dev/null" \
    INCLUDE_PATH="$LIB_PATH/zlib/windows/include" \
    LIBRARY_PATH="$LIB_PATH/zlib/windows/lib" 1> /dev/null

# Remove extra pkgconfig
rm -rf "$LIB_PATH/zlib/windows/lib/pkgconfig"

# ==== Clean Up ====
cd ../
rm -rf "zlib-$version"
rm -f "zlib-$version.tar.gz"

echo "✅ Successfully built Zlib v$version library."