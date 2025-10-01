#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update
sudo DEBIAN_FRONTEND=noninteractive apt install \
    build-essential \
    mingw-w64 nasm \
    zlib1g-dev \
    php-cgi \
    -y --no-install-recommends

echo "✅ Successfully installed workflow dependencies."