#!/usr/bin/env bash

set -e

# CD into project directory
cd "$(dirname "$0")/../"

# Get current versions
BROTLI_CURRENT=$(  grep -Po "(?<=^BROTLI=)(.*)$" ./build_tools/dependencies.txt )
OPENSSL_CURRENT=$( grep -Po "(?<=^OPENSSL=)(.*)$" ./build_tools/dependencies.txt )
PUGIXML_CURRENT=$( grep -Po "(?<=^PUGIXML=)(.*)$" ./build_tools/dependencies.txt )
ZLIB_CURRENT=$(    grep -Po "(?<=^ZLIB=)(.*)$" ./build_tools/dependencies.txt )
ZSTD_CURRENT=$(    grep -Po "(?<=^ZSTD=)(.*)$" ./build_tools/dependencies.txt )
LLVM_MINGW_CURRENT=$( grep -Po "(?<=^LLVM_MINGW=)(.*)$" ./build_tools/dependencies.txt )

DEPS_CHANGED=()

# Check Brotli
BROTLI_LATEST=$( curl -Ls https://api.github.com/repos/google/brotli/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+\.\d+)(?=\")" )
if [[ -n "$BROTLI_LATEST" && "$BROTLI_CURRENT" != "$BROTLI_LATEST" ]]; then
    sed -i "s|BROTLI=$BROTLI_CURRENT|BROTLI=$BROTLI_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("Brotli v$BROTLI_LATEST")
fi

# Check OpenSSL
OPENSSL_LATEST=$( curl -Ls https://api.github.com/repos/openssl/openssl/releases/latest | grep -Po "\"tag_name\":\s*\"openssl-\K(\d+\.\d+\.\d+)(?=\")" )
if [[ -n "$OPENSSL_LATEST" && "$OPENSSL_CURRENT" != "$OPENSSL_LATEST" ]]; then
    sed -i "s|OPENSSL=$OPENSSL_CURRENT|OPENSSL=$OPENSSL_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("OpenSSL v$OPENSSL_LATEST")
fi

# Check PugiXML
PUGIXML_LATEST=$( curl -Ls https://api.github.com/repos/zeux/pugixml/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+)(?=\")" )
if [[ -n "$PUGIXML_LATEST" && "$PUGIXML_CURRENT" != "$PUGIXML_LATEST" ]]; then
    sed -i "s|PUGIXML=$PUGIXML_CURRENT|PUGIXML=$PUGIXML_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("PugiXML v$PUGIXML_LATEST")
fi

# Check Zlib
ZLIB_LATEST=$( curl -Ls https://api.github.com/repos/madler/zlib/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+(\.\d+)?)(?=\")" )
if [[ -n "$ZLIB_LATEST" && "$ZLIB_CURRENT" != "$ZLIB_LATEST" ]]; then
    sed -i "s|ZLIB=$ZLIB_CURRENT|ZLIB=$ZLIB_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("Zlib v$ZLIB_LATEST")
fi

# Check Zstandard
ZSTD_LATEST=$( curl -Ls https://api.github.com/repos/meta/zstd/releases/latest | grep -Po "\"tag_name\":\s*\"v\K(\d+\.\d+\.\d+)(?=\")" )
if [[ -n "$ZSTD_LATEST" && "$ZSTD_CURRENT" != "$ZSTD_LATEST" ]]; then
    sed -i "s|ZSTD=$ZSTD_CURRENT|ZSTD=$ZSTD_LATEST|" ./build_tools/dependencies.txt
    DEPS_CHANGED+=("Zstandard v$ZSTD_LATEST")
fi

# Check LLVM-MinGW releases for both Ubuntu 22.04 host archives and digests
if LLVM_MINGW_PINS=$(curl -fsSL --max-time 30 'https://api.github.com/repos/mstorsjo/llvm-mingw/releases?per_page=30' |
    jq -r --arg current "$LLVM_MINGW_CURRENT" '
        def checksum($name):
            first(.assets[] | select(.name == $name and .state == "uploaded") | .digest // empty
                | select(test("^sha256:[0-9a-f]{64}$")) | ltrimstr("sha256:"));
        [
            .[] | select(.draft == false and .prerelease == false)
            | .tag_name as $tag
            | select(($tag | test("^[0-9]{8}$")) and $tag > $current)
            | "llvm-mingw-\($tag)-ucrt-ubuntu-22.04-" as $prefix
            | [$tag, checksum($prefix + "aarch64.tar.xz"), checksum($prefix + "x86_64.tar.xz")]
            | select(length == 3)
        ] | max_by(.[0]) // empty | @tsv
    '); then
    if [[ -n "$LLVM_MINGW_PINS" ]]; then
        IFS=$'\t' read -r version arm64_checksum x86_64_checksum <<< "$LLVM_MINGW_PINS"
        sed -i \
            -e "s/^LLVM_MINGW=.*/LLVM_MINGW=$version/" \
            -e "s/^LLVM_MINGW_AARCH64_SHA256=.*/LLVM_MINGW_AARCH64_SHA256=$arm64_checksum/" \
            -e "s/^LLVM_MINGW_X86_64_SHA256=.*/LLVM_MINGW_X86_64_SHA256=$x86_64_checksum/" \
            ./build_tools/dependencies.txt
        DEPS_CHANGED+=("LLVM-MinGW $version")
    fi
else
    echo "Unable to check LLVM-MinGW releases. Keeping the current toolchain pin." >&2
fi

# Echo all changed deps
echo "Updated the following dependency requirements:"
if [[ ${#DEPS_CHANGED[@]} -gt 0 ]]; then
    printf -v joined '%s, ' "${DEPS_CHANGED[@]}"
    echo "${joined%, }"
else
    echo "None"
fi
