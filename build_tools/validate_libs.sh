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

# === Brotli ===

if ! grep -q "^brotli=$BROTLI_VERSION" artifacts.lock; then
    echo "Update Brotli to $BROTLI_VERSION via \`make static_brotli\`"
    exit 1
fi

# === OpenSSL ===

if ! grep -q "^openssl=$OPENSSL_VERSION" artifacts.lock; then
    echo "Update OpenSSL to $OPENSSL_VERSION via \`make static_openssl\`"
    exit 1
fi

# === zlib ===

if ! grep -q "^zlib=$ZLIB_VERSION" artifacts.lock; then
    echo "Update zlib to $ZLIB_VERSION via \`make static_zlib\`"
    exit 1
fi

if [ -z "$1" ] || [ "$1" != "--q" ]; then
    echo "✅ All artifacts pass version validation."
fi