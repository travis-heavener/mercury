#!/usr/bin/env bash

set -euo pipefail

# Require destination directory
if [[ $# != 1 ]]; then
    echo "Usage: $0 <install-directory>" >&2
    exit 2
fi

# Read pinned release and select checksum for host architecture
pins="$(dirname "$0")/dependencies.txt"
version=$(sed -n 's/^LLVM_MINGW=//p' "$pins")
host_arch=$(uname -m)
case "$host_arch" in
    aarch64) checksum_key=LLVM_MINGW_AARCH64_SHA256 ;;
    x86_64) checksum_key=LLVM_MINGW_X86_64_SHA256 ;;
    *) echo "LLVM-MinGW is unavailable for Linux $host_arch" >&2; exit 1 ;;
esac
# Validate pins before constructing download URL
checksum=$(sed -n "s/^${checksum_key}=//p" "$pins")
if [[ ! "$version" =~ ^[0-9]{8}$ || ! "$checksum" =~ ^[0-9a-f]{64}$ ]]; then
    echo "Invalid LLVM-MinGW version or checksum in $pins" >&2
    exit 1
fi

# Download archive into temporary directory and clean up on exit
asset="llvm-mingw-$version-ucrt-ubuntu-22.04-$host_arch.tar.xz"
download_dir=$(mktemp -d)
trap 'rm -rf -- "$download_dir"' EXIT
wget -q -O "$download_dir/$asset" "https://github.com/mstorsjo/llvm-mingw/releases/download/$version/$asset"

# Verify archive before extracting toolchain
printf '%s  %s\n' "$checksum" "$download_dir/$asset" | sha256sum --check
mkdir -p "$1"
tar -xJf "$download_dir/$asset" --strip-components=1 -C "$1"
