#!/usr/bin/env bash

#
# Validate the version of static libraries
#

# CD into project directory
cd "$(dirname "$0")/../"

quiet=false
check_windows=false
for option in "$@"; do
    case "$option" in
        --q) quiet=true ;;
        --windows) check_windows=true ;;
        *) echo "Unknown option: $option" >&2; exit 2 ;;
    esac
done
host_arch=$(uname -m)

# Declare versions
BROTLI_VERSION=$(    cat ./build_tools/dependencies.txt | grep -Po "(?<=^BROTLI=)(.*)$" )
OPENSSL_VERSION=$(   cat ./build_tools/dependencies.txt | grep -Po "(?<=^OPENSSL=)(.*)$" )
ZLIB_VERSION=$(      cat ./build_tools/dependencies.txt | grep -Po "(?<=^ZLIB=)(.*)$" )
PUGIXML_VERSION=$(   cat ./build_tools/dependencies.txt | grep -Po "(?<=^PUGIXML=)(.*)$" )
ZSTD_VERSION=$(      cat ./build_tools/dependencies.txt | grep -Po "(?<=^ZSTD=)(.*)$" )
GPP_VERSION=$(       cat ./build_tools/dependencies.txt | grep -Po "(?<=^GPP=)(.*)$" )
MINGW_W64_VERSION=$( cat ./build_tools/dependencies.txt | grep -Po "(?<=^MINGW_W64=)(.*)$" )

if ! cd libs 2>/dev/null || [[ ! -f artifacts.lock ]]; then
    echo 'Missing library artifacts. Run `make libs`.'
    exit 1
fi

# Returns success for matching version and host architecture
matches_native_artifact() {
    local name="$1" version="$2"
    grep -Fxq "$name=$version@$host_arch" artifacts.lock ||
        { [[ "$host_arch" == x86_64 ]] && grep -Fxq "$name=$version" artifacts.lock; }
}

# === Brotli ===

HAS_FAILED="false"

if ! matches_native_artifact brotli "$BROTLI_VERSION"; then
    echo "Update Brotli to $BROTLI_VERSION via \`make lib_brotli\`"
    HAS_FAILED="true"
elif [[ ! -s brotli/linux/lib/libbrotlienc.a || ! -s brotli/linux/lib/libbrotlicommon.a ]]; then
    echo "Missing Brotli $BROTLI_VERSION, build it via \`make lib_brotli\`"
    HAS_FAILED="true"
fi

# === OpenSSL ===

if ! matches_native_artifact openssl "$OPENSSL_VERSION"; then
    echo "Update OpenSSL to $OPENSSL_VERSION via \`make lib_openssl\`"
    HAS_FAILED="true"
elif [[ ! -s openssl/linux/lib/libssl.a || ! -s openssl/linux/lib/libcrypto.a ]]; then
    echo "Missing OpenSSL $OPENSSL_VERSION, build it via \`make lib_openssl\`"
    HAS_FAILED="true"
fi

# === zlib ===

if ! matches_native_artifact zlib "$ZLIB_VERSION"; then
    echo "Update zlib to $ZLIB_VERSION via \`make lib_zlib\`"
    HAS_FAILED="true"
elif [[ ! -s zlib/linux/lib/libz.a ]]; then
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

if ! matches_native_artifact zstd "$ZSTD_VERSION"; then
    echo "Update Zstandard to $ZSTD_VERSION via \`make lib_zstd\`"
    HAS_FAILED="true"
elif [[ ! -s zstd/linux/lib/libzstd.a ]]; then
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

if [[ "$check_windows" == true ]]; then
    missing_windows_libs=()
    for library in brotli/windows/lib/libbrotlienc.a brotli/windows/lib/libbrotlicommon.a \
        openssl/windows/lib/libssl.a openssl/windows/lib/libcrypto.a zlib/windows/lib/libz.a zstd/windows/lib/libzstd.a; do
        if [[ ! -s "$library" ]]; then
            missing_windows_libs+=("$library")
        fi
    done
    if (( ${#missing_windows_libs[@]} > 0 )); then
        printf 'Missing Windows library: %s\n' "${missing_windows_libs[@]}"
        echo 'Run `make libs_no_deps` with the Windows toolchain on PATH.'
        HAS_FAILED="true"
    fi

    if [[ "$host_arch" == aarch64 ]]; then
        for tool in clang clang++ ar ranlib windres; do
            if ! command -v "aarch64-w64-mingw32-$tool" >/dev/null 2>&1; then
                echo "Missing ARM64 Windows tool: aarch64-w64-mingw32-$tool"
                HAS_FAILED="true"
            fi
        done
    elif command -v dpkg >/dev/null 2>&1; then
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
fi

# === Close up ===

if [ $HAS_FAILED == "true" ]; then
    exit 1
fi

if [[ "$quiet" == false ]]; then
    echo "✅ All artifacts pass version validation."
fi
