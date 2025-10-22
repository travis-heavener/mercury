#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update
sudo apt install upx \
    build-essential perl wget \
    mingw-w64 nasm \
    cmake make \
    g++ \
    zip \
    openssl \
    python3 python3-brotli python3-zstandard python3-psutil \
    gzip \
    php-cgi -y

echo "✅ Successfully installed dependencies."