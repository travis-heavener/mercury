#!/usr/bin/env bash

# CD into script directory
cd "$(dirname "$0")/"

# Installs *all* deps needed to rebuild Mercury
PKG_MGR=$("./tools/getpm")

case "$PKG_MGR" in
    apt)
        sudo apt-get update
        sudo apt-get install -y upx \
            build-essential perl wget \
            mingw-w64 nasm \
            cmake make \
            g++ \
            zip \
            openssl \
            python3 python3-brotli python3-zstandard python3-psutil \
            php-cgi
        ;;
    pacman)
        sudo pacman -Syu --noconfirm
        sudo pacman -S --noconfirm upx \
            base-devel perl wget \
            mingw-w64 nasm \
            cmake make \
            gcc \
            zip \
            openssl \
            python python-brotli python-zstandard python-psutil \
            php-cgi
        ;;
    dnf)
        sudo dnf makecache -y
        sudo dnf install -y upx \
            rpmdevtools perl wget \
            mingw64-gcc-c++ libstdc++-static nasm \
            cmake make \
            g++ \
            zip \
            openssl \
            python3 python3-brotli python3-zstandard python3-psutil \
            php-cgi
        ;;
    *)
        echo "Unknown package manager: $PKG_MGR" >&2
        exit 1
        ;;
esac

echo "✅ Successfully installed dependencies."
