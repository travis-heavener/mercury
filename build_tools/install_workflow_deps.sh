#!/bin/bash

# Installs *all* deps needed to rebuild Mercury
sudo apt update
sudo DEBIAN_FRONTEND=noninteractive apt install \
    mingw-w64 \
    -y --no-install-recommends

echo "✅ Successfully installed workflow dependencies."