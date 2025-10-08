#!/bin/bash

#
# Validate the version of static libraries
#

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs

# Declare versions
BROTLI_VERSION="1.1.0"
OPENSSL_VERSION="3.6.0"
ZLIB_VERSION="1.3.1"
PUGIXML_VERSION="1.15"
ZSTD_VERSION="1.5.7"

GPP_VERSION="13.3.0"
MINGW_W64_VERSION="11.0.1"

# Verify artifacts.lock exists
if [ ! -e "artifacts.lock" ]; then
    touch artifacts.lock
    echo "" | gzip | base64 > artifacts.lock
fi

# Unpack artifacts
cat artifacts.lock | base64 --decode | gunzip > artifacts.raw

# Remove CRLF for LF
sed -i 's/\r//g' artifacts.raw

# === Brotli ===

HAS_FAILED="false"

if ! grep -q "^brotli=${BROTLI_VERSION}\$" artifacts.raw; then
    echo "Update Brotli to $BROTLI_VERSION via \`make lib_brotli\`"
    HAS_FAILED="true"
fi

# === OpenSSL ===

if ! grep -q "^openssl=${OPENSSL_VERSION}\$" artifacts.raw; then
    echo "Update OpenSSL to $OPENSSL_VERSION via \`make lib_openssl\`"
    HAS_FAILED="true"
fi

# === zlib ===

if ! grep -q "^zlib=${ZLIB_VERSION}\$" artifacts.raw; then
    echo "Update zlib to $ZLIB_VERSION via \`make lib_zlib\`"
    HAS_FAILED="true"
fi

# === PugiXML ===

if ! grep -q "^pugixml=${PUGIXML_VERSION}\$" artifacts.raw; then
    echo "Update PugiXML to $PUGIXML_VERSION via \`make lib_pugixml\`"
    HAS_FAILED="true"
fi

# === Zstandard ===

if ! grep -q "^zstd=${ZSTD_VERSION}\$" artifacts.raw; then
    echo "Update Zstandard to $ZSTD_VERSION via \`make lib_zstd\`"
    HAS_FAILED="true"
fi

# === g++ ===

CURRENT_GPP=$(g++ --version | head -1 | grep -oP "\d+\.\d+\.\d+" | head -1)

if dpkg --compare-versions "$CURRENT_GPP" lt "$GPP_VERSION"; then
    echo "Update g++ to $GPP_VERSION or newer (currently $CURRENT_GPP)"
    HAS_FAILED="true"
fi

# === mingw-w64 ===

CURRENT_MINGW_W64=$(dpkg -s mingw-w64 | grep -oP "(?<=Version: )\d+\.\d+\.\d+")

if dpkg --compare-versions "$CURRENT_MINGW_W64" lt "$MINGW_W64_VERSION"; then
    echo "Update mingw-w64 to $MINGW_W64_VERSION or newer (currently $CURRENT_MINGW_W64)"
    HAS_FAILED="true"
fi

# === Close up ===

rm -f artifacts.raw

if [ $HAS_FAILED == "true" ]; then
    exit 1
fi

if [ -z "$1" ] || [ "$1" != "--q" ]; then
    echo "✅ All artifacts pass version validation."
fi