#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs
LIB_PATH=$(pwd)

# Update artifacts.lock
version="1.3.1"
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

# ==== Config ====
ZLIB_DIR="$LIB_PATH/zlib-repo"
MINGW_DIR="/usr/x86_64-w64-mingw32"

wget -q --no-check-certificate https://zlib.net/zlib-$version.tar.gz
tar -xzf zlib-$version.tar.gz
echo "Extracted archive."

# Remove tarball & rename
rm zlib-$version.tar.gz
mv zlib-$version "$ZLIB_DIR" && cd "$ZLIB_DIR"

# ==== Patch Makefile.gcc ====
MAKEFILE_PATH="win32/Makefile.gcc"

# Replace PREFIX line
sed -i 's/^PREFIX *=.*$/PREFIX=x86_64-w64-mingw32-/' "$MAKEFILE_PATH"

# Replace CC line
sed -i 's/^CC *=.*$/CC=\$(PREFIX)gcc-win32/' "$MAKEFILE_PATH"

# ==== Build ====
echo "Building zlib binaries."
make -B -f win32/Makefile.gcc \
    BINARY_PATH="$MINGW_DIR/bin" \
    INCLUDE_PATH="$MINGW_DIR/include" \
    LIBRARY_PATH="$MINGW_DIR/lib" 1> /dev/null

# ==== Install to Mingw files ====
sudo make -B -f win32/Makefile.gcc install \
    BINARY_PATH="$MINGW_DIR/bin" \
    INCLUDE_PATH="$MINGW_DIR/include" \
    LIBRARY_PATH="$MINGW_DIR/lib" 1> /dev/null

# ==== Clean Up ====
rm -rf "$ZLIB_DIR"

echo "✅ Successfully built Zlib v$version library."