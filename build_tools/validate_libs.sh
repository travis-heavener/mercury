#!/bin/bash

#
# Validate the version of static libraries
#
# **Note that artifacts.lock is alphabetized**
#

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "static_libs" ]; then
    mkdir static_libs
fi

cd static_libs

# Declare versions
BROTLI_VERSION="1.1.0"
OPENSSL_VERSION="3.5.2"
ZLIB_VERSION="1.3.1"

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
    echo "Update Brotli to $BROTLI_VERSION via \`make static_brotli\`"
    HAS_FAILED="true"
fi

# === OpenSSL ===

if ! grep -q "^openssl=${OPENSSL_VERSION}\$" artifacts.raw; then
    echo "Update OpenSSL to $OPENSSL_VERSION via \`make static_openssl\`"
    HAS_FAILED="true"
fi

# === zlib ===

if ! grep -q "^zlib=${ZLIB_VERSION}\$" artifacts.raw; then
    echo "Update zlib to $ZLIB_VERSION via \`make static_zlib\`"
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