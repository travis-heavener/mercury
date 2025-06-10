#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "static_libs" ]; then
    mkdir static_libs
fi

cd static_libs
LIB_PATH=$(pwd)

# ==== Config ====
ZLIB_REPO="https://github.com/luvit/zlib"
ZLIB_DIR="$LIB_PATH/zlib-repo"
MINGW_DIR="/usr/x86_64-w64-mingw32"

# ==== Clean old stuff ====
rm -rf "$ZLIB_DIR"
mkdir -p "$ZLIB_DIR"

# ==== Clone zlib ====
echo "Cloning zlib repo."
git clone --q "$ZLIB_REPO" "$ZLIB_DIR"
cd "$ZLIB_DIR"

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

echo "✅ Successfully built static zlib binaries."