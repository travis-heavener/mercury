#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update 1> /dev/null
sudo apt install upx \
    build-essential perl wget \
    mingw-w64 nasm \
    cmake make \
    g++ \
    zip \
    openssl \
    python3 python3-brotli python3-zstandard python3-psutil \
    gzip \
    php-cgi -y 1> /dev/null

echo "✅ Successfully installed dependencies."