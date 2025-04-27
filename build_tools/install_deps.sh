#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update
sudo apt install upx \
    build-essential perl wget \
    mingw-w64 nasm \
    cmake \
    zlib1g-dev \
    g++ \
    mingw-w64 -y

echo "✅ Successfully installed dependencies."