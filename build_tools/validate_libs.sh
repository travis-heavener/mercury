#!/bin/bash

#
# Validate the version of static libraries
#
# **Note that artifacts.lock is alphabetized**
#

# CD into project directory
cd "$(dirname "$0")/../"

BROTLI_VERSION="1.1.0"
OPENSSL_VERSION="3.5.2"
ZLIB_VERSION="1.3.1"

# Verify artifacts.lock exists
if [ ! -e "artifacts.lock" ]; then
    echo "Missing artifacts.lock in project root"
    exit 1
fi

# Remove CRLF for LF
sed -i 's/\r//g' artifacts.lock

# === Brotli ===

HAS_FAILED="false"

if ! grep -q "^brotli=${BROTLI_VERSION}\$" artifacts.lock; then
    echo "Update Brotli to $BROTLI_VERSION via \`make static_brotli\`"
    HAS_FAILED="true"
fi

# === OpenSSL ===

if ! grep -q "^openssl=${OPENSSL_VERSION}\$" artifacts.lock; then
    echo "Update OpenSSL to $OPENSSL_VERSION via \`make static_openssl\`"
    HAS_FAILED="true"
fi

# === zlib ===

if ! grep -q "^zlib=${ZLIB_VERSION}\$" artifacts.lock; then
    echo "Update zlib to $ZLIB_VERSION via \`make static_zlib\`"
    HAS_FAILED="true"
fi

if [ $HAS_FAILED == "true" ]; then
    exit 1
fi

if [ -z "$1" ] || [ "$1" != "--q" ]; then
    echo "✅ All artifacts pass version validation."
fi