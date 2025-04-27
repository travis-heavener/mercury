#!/bin/bash

# ==== Config ====
ZLIB_REPO="https://github.com/luvit/zlib"
ZLIB_DIR="$HOME/zlib-repo"
MINGW_DIR="/usr/x86_64-w64-mingw32"

# ==== Clean old stuff ====
rm -rf "$ZLIB_DIR"
mkdir -p "$ZLIB_DIR"

# ==== Clone zlib ====
git clone "$ZLIB_REPO" "$ZLIB_DIR"
cd "$ZLIB_DIR"

# ==== Patch Makefile.gcc ====
MAKEFILE_PATH="win32/Makefile.gcc"

# Replace PREFIX line
sed -i 's/^PREFIX *=.*$/PREFIX=x86_64-w64-mingw32-/' "$MAKEFILE_PATH"

# Replace CC line
sed -i 's/^CC *=.*$/CC=\$(PREFIX)gcc-win32/' "$MAKEFILE_PATH"

# ==== Build ====
make -B -f win32/Makefile.gcc \
    BINARY_PATH="$MINGW_DIR/bin" \
    INCLUDE_PATH="$MINGW_DIR/include" \
    LIBRARY_PATH="$MINGW_DIR/lib"

# ==== Install to Mingw files ====
sudo make -B -f win32/Makefile.gcc install \
    BINARY_PATH="$MINGW_DIR/bin" \
    INCLUDE_PATH="$MINGW_DIR/include" \
    LIBRARY_PATH="$MINGW_DIR/lib"

# ==== Clean Up ====
rm -rf "$ZLIB_DIR"

echo "✅ Successfully built static zlib binaries."