#!/bin/bash

# CD to user home
cd ~

# Get OpenSSL source
wget https://www.openssl.org/source/openssl-3.5.0.tar.gz
tar -xvzf openssl-3.5.0.tar.gz
cd openssl-3.5.0

# Configure static build
./Configure linux-x86_64 no-shared no-dso no-ssl3 no-comp --prefix=$HOME/openssl-static

# Build static
make -j$(nproc)

# Install binaries
make install_sw