#!/usr/bin/env bash

set -e

# CD into script directory
cd "$(dirname "$0")/"

# Installs *all* deps needed to rebuild Mercury
PKG_MGR=$("./tools/getpm")

mingw_packages=()
if [[ "$LINUX_ONLY" != 1 && "$(uname -m)" == x86_64 ]]; then
    case "$PKG_MGR" in
        apt|pacman) mingw_packages=(mingw-w64 nasm) ;;
        dnf) mingw_packages=(mingw64-gcc-c++ nasm) ;;
    esac
fi

case "$PKG_MGR" in
    apt)
        sudo apt-get update
        zstandard_package=()
        if apt-cache show python3-zstandard >/dev/null 2>&1; then
            zstandard_package=(python3-zstandard)
        else
            echo "Install Python zstandard separately before running the test suite." >&2
        fi
        sudo apt-get install -y \
            build-essential perl wget \
            "${mingw_packages[@]}" \
            cmake make \
            g++ \
            zip \
            openssl \
            python3 python3-brotli "${zstandard_package[@]}" python3-psutil \
            php-cgi php-fpm \
            ccache
        ;;
    pacman)
        sudo pacman -Syu --noconfirm
        sudo pacman -S --noconfirm \
            base-devel perl wget \
            "${mingw_packages[@]}" \
            cmake make \
            gcc \
            zip \
            openssl \
            python python-brotli python-zstandard python-psutil \
            php-cgi php-fpm \
            ccache
        ;;
    dnf)
        sudo dnf makecache -y
        sudo dnf install -y \
            rpmdevtools perl wget \
            "${mingw_packages[@]}" libstdc++-static \
            cmake make \
            g++ \
            zip \
            openssl \
            python3 python3-brotli python3-zstandard python3-psutil \
            php-cgi php-fpm \
            ccache
        ;;
    *)
        echo "Unsupported package manager: $PKG_MGR" >&2
        exit 1
        ;;
esac

echo "✅ Successfully installed dependencies."
