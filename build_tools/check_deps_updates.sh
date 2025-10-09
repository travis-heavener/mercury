#!/bin/bash
set -euo pipefail

# CD into project directory
cd "$(dirname "$0")/../"

# Get current versions
BROTLI_CURRENT=$(  cat ./build_tools/dependencies.txt | grep -Po "(?<=^BROTLI=)(.*)$" )
OPENSSL_CURRENT=$( cat ./build_tools/dependencies.txt | grep -Po "(?<=^OPENSSL=)(.*)$" )
PUGIXML_CURRENT=$( cat ./build_tools/dependencies.txt | grep -Po "(?<=^PUGIXML=)(.*)$" )
ZLIB_CURRENT=$(    cat ./build_tools/dependencies.txt | grep -Po "(?<=^ZLIB=)(.*)$" )
ZSTD_CURRENT=$(    cat ./build_tools/dependencies.txt | grep -Po "(?<=^ZSTD=)(.*)$" )

DEPS_CHANGED=()

# Check Brotli
BROTLI_LATEST=$( curl -s https://api.github.com/repos/google/brotli/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+\.\d+)(?=\")" )
if [ "$BROTLI_CURRENT" != "$BROTLI_LATEST" ]; then
    sed -i "s|BROTLI=$BROTLI_CURRENT|BROTLI=$BROTLI_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("Brotli v$BROTLI_LATEST")
fi

# Check OpenSSL
OPENSSL_LATEST=$( curl -s https://api.github.com/repos/openssl/openssl/releases/latest | grep -Po "\"tag_name\":\s*\"openssl-\K(\d+\.\d+\.\d+)(?=\")" )
if [ "$OPENSSL_CURRENT" != "$OPENSSL_LATEST" ]; then
    sed -i "s|OPENSSL=$OPENSSL_CURRENT|OPENSSL=$OPENSSL_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("OpenSSL v$OPENSSL_LATEST")
fi

# Check PugiXML
PUGIXML_LATEST=$( curl -s https://api.github.com/repos/zeux/pugixml/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+)(?=\")" )
if [ "$PUGIXML_CURRENT" != "$PUGIXML_LATEST" ]; then
    sed -i "s|PUGIXML=$PUGIXML_CURRENT|PUGIXML=$PUGIXML_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("PugiXML v$PUGIXML_LATEST")
fi

# Check Zlib
ZLIB_LATEST=$( curl -s https://api.github.com/repos/madler/zlib/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+\.\d+)(?=\")" )
if [ "$ZLIB_CURRENT" != "$ZLIB_LATEST" ]; then
    sed -i "s|ZLIB=$ZLIB_CURRENT|ZLIB=$ZLIB_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("Zlib v$ZLIB_LATEST")
fi

# Check Zstandard
ZSTD_LATEST=$( curl -s https://api.github.com/repos/facebook/zstd/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+\.\d+)(?=\")" )
if [ "$ZSTD_CURRENT" != "$ZSTD_LATEST" ]; then
    sed -i "s|ZSTD=$ZSTD_CURRENT|ZSTD=$ZSTD_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("Zstandard v$ZSTD_LATEST")
fi

# Echo all changed deps
if [[ ${#DEPS_CHANGED[@]} -gt 0 ]]; then
    echo "Updated the following dependency requirements:"
    printf -v joined '%s, ' "${DEPS_CHANGED[@]}"
    echo "${joined%, }"
fi