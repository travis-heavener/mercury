#!/usr/bin/env bash

# CD into script directory
cd "$(dirname "$0")/"

# Installs *all* deps needed to rebuild Mercury
PKG_MGR=$("./tools/getpm")

case "$PKG_MGR" in
    apt)
        sudo apt-get update 1>/dev/null
        sudo apt-get install -y upx \
            build-essential perl wget \
            mingw-w64 nasm \
            cmake make \
            g++ \
            zip \
            openssl \
            python3 python3-brotli python3-zstandard python3-psutil \
            php-cgi 1>/dev/null
        ;;
    pacman)
        sudo pacman -Syu --noconfirm 1>/dev/null
        sudo pacman -S --noconfirm upx \
            base-devel perl wget \
            mingw-w64 nasm \
            cmake make \
            gcc \
            zip \
            openssl \
            python python-brotli python-zstandard python-psutil \
            php-cgi 1>/dev/null
        ;;
    # Not implemented yet
    # dnf)
    #     sudo dnf makecache -y 1>/dev/null
    #     sudo dnf install -y $PKGS 1>/dev/null
    #     ;;
    # yum)
    #     sudo yum makecache -y 1>/dev/null
    #     sudo yum install -y $PKGS 1>/dev/null
    #     ;;
    *)
        echo "Unknown package manager: $PKG_MGR" >&2
        exit 1
        ;;
esac

echo "✅ Successfully installed dependencies."