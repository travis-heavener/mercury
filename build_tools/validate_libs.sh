#!/usr/bin/env bash

#
# Validate the version of static libraries
#

# CD into project directory
cd "$(dirname "$0")/../"

# Declare versions
BROTLI_VERSION=$(    cat ./build_tools/dependencies.txt | grep -Po "(?<=^BROTLI=)(.*)$" )
OPENSSL_VERSION=$(   cat ./build_tools/dependencies.txt | grep -Po "(?<=^OPENSSL=)(.*)$" )
ZLIB_VERSION=$(      cat ./build_tools/dependencies.txt | grep -Po "(?<=^ZLIB=)(.*)$" )
PUGIXML_VERSION=$(   cat ./build_tools/dependencies.txt | grep -Po "(?<=^PUGIXML=)(.*)$" )
ZSTD_VERSION=$(      cat ./build_tools/dependencies.txt | grep -Po "(?<=^ZSTD=)(.*)$" )
GPP_VERSION=$(       cat ./build_tools/dependencies.txt | grep -Po "(?<=^GPP=)(.*)$" )
MINGW_W64_VERSION=$( cat ./build_tools/dependencies.txt | grep -Po "(?<=^MINGW_W64=)(.*)$" )

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs

# Verify artifacts.lock exists
if [ ! -e "artifacts.lock" ]; then
    echo "" > artifacts.lock
fi

# === Brotli ===

HAS_FAILED="false"

if ! grep -q "^brotli=${BROTLI_VERSION}\$" artifacts.lock; then
    echo "Update Brotli to $BROTLI_VERSION via \`make lib_brotli\`"
    HAS_FAILED="true"
elif [[ ! -d "brotli" ]]; then
    echo "Missing Brotli $BROTLI_VERSION, build it via \`make lib_brotli\`"
    HAS_FAILED="true"
fi

# === OpenSSL ===

if ! grep -q "^openssl=${OPENSSL_VERSION}\$" artifacts.lock; then
    echo "Update OpenSSL to $OPENSSL_VERSION via \`make lib_openssl\`"
    HAS_FAILED="true"
elif [[ ! -d "openssl" ]]; then
    echo "Missing OpenSSL $OPENSSL_VERSION, build it via \`make lib_openssl\`"
    HAS_FAILED="true"
fi

# === zlib ===

if ! grep -q "^zlib=${ZLIB_VERSION}\$" artifacts.lock; then
    echo "Update zlib to $ZLIB_VERSION via \`make lib_zlib\`"
    HAS_FAILED="true"
elif [[ ! -d "zlib" ]]; then
    echo "Missing zlib $ZLIB_VERSION, build it via \`make lib_zlib\`"
    HAS_FAILED="true"
fi

# === PugiXML ===

if ! grep -q "^pugixml=${PUGIXML_VERSION}\$" artifacts.lock; then
    echo "Update PugiXML to $PUGIXML_VERSION via \`make lib_pugixml\`"
    HAS_FAILED="true"
elif [[ ! -d "pugixml" ]]; then
    echo "Missing PugiXML $PUGIXML_VERSION, build it via \`make lib_pugixml\`"
    HAS_FAILED="true"
fi

# === Zstandard ===

if ! grep -q "^zstd=${ZSTD_VERSION}\$" artifacts.lock; then
    echo "Update Zstandard to $ZSTD_VERSION via \`make lib_zstd\`"
    HAS_FAILED="true"
elif [[ ! -d "zstd" ]]; then
    echo "Missing Zstandard $ZSTD_VERSION, build it via \`make lib_zstd\`"
    HAS_FAILED="true"
fi

# === g++ ===

CURRENT_GPP=$(g++ --version | head -1 | grep -oP "\d+\.\d+\.\d+" | head -1)

if command -v dpkg >/dev/null 2>&1; then
    if dpkg --compare-versions "$CURRENT_GPP" lt "$GPP_VERSION"; then
        echo "Update g++ to $GPP_VERSION or newer (currently $CURRENT_GPP)"
        HAS_FAILED="true"
    fi
elif command -v vercmp >/dev/null 2>&1; then
    if [[ $(vercmp "$CURRENT_GPP" "$GPP_VERSION") -lt 0 ]]; then
        echo "Update g++ to $GPP_VERSION or newer (currently $CURRENT_GPP)"
        HAS_FAILED="true"
    fi
else
    if [[ $(rpmdev-vercmp "$CURRENT_GPP" "$GPP_VERSION" >/dev/null 2>&1) -eq 12 ]]; then
        echo "Update g++ to $GPP_VERSION or newer (currently $CURRENT_GPP)"
        HAS_FAILED="true"
    fi
fi

# === mingw-w64 ===

if command -v dpkg >/dev/null 2>&1; then
    CURRENT_MINGW_W64=$(dpkg -s mingw-w64 | grep -oP "(?<=Version: )\d+\.\d+\.\d+")
    if dpkg --compare-versions "$CURRENT_MINGW_W64" lt "$MINGW_W64_VERSION"; then
        echo "Update mingw-w64 to $MINGW_W64_VERSION or newer (currently $CURRENT_MINGW_W64)"
        HAS_FAILED="true"
    fi
elif command -v pacman >/dev/null 2>&1; then
    CURRENT_MINGW_W64=$(pacman -Qi mingw-w64-gcc | grep -Po "^Version\s*:\s*\K\d+\.\d+\.\d+")
    if [[ $(vercmp "$CURRENT_MINGW_W64" "$MINGW_W64_VERSION") -lt 0 ]]; then
        echo "Update mingw-w64 to $MINGW_W64_VERSION or newer (currently $CURRENT_MINGW_W64)"
        HAS_FAILED="true"
    fi
else
    CURRENT_MINGW_W64=$(rpm -q mingw64-gcc-c++ | grep -Po "(?<=mingw64\-gcc\-c\+\+\-)\d+\.\d+\.\d+")
    if [[ $(rpmdev-vercmp "$CURRENT_MINGW_W64" "$MINGW_W64_VERSION" >/dev/null 2>&1) -eq 12 ]]; then
        echo "Update mingw-w64 to $MINGW_W64_VERSION or newer (currently $CURRENT_MINGW_W64)"
        HAS_FAILED="true"
    fi
fi

# === Close up ===

if [ $HAS_FAILED == "true" ]; then
    exit 1
fi

if [ -z "$1" ] || [ "$1" != "--q" ]; then
    echo "✅ All artifacts pass version validation."
fi