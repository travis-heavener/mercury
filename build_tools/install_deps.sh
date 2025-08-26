#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update
sudo apt install upx \
    build-essential perl wget \
    mingw-w64 nasm \
    cmake make \
    zlib1g-dev \
    g++ \
    zip \
    python3 \
    gzip \
    php8.3-fpm -y

echo "✅ Successfully installed dependencies."