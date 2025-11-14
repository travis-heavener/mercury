#!/usr/bin/env bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update 1> /dev/null
sudo DEBIAN_FRONTEND=noninteractive apt install \
    mingw-w64 \
    -y --no-install-recommends 1> /dev/null

echo "✅ Successfully installed workflow dependencies."