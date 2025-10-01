#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update
sudo apt install \
    build-essential \
    mingw-w64 nasm \
    zlib1g-dev \
    php-cgi -y

echo "✅ Successfully installed workflow dependencies."