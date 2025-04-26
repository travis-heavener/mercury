#!/bin/bash

# CD to user home
cd ~

# Clean existing
if [ -d "~/openssl-3.5.0" ]; then
    rm -rf openssl-3.5.0
fi

if [ -f "~/openssl-3.5.0.tar.gz" ]; then
    rm -f openssl-3.5.0.tar.gz
fi

# ==== Linux Build ====

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

# ==== Windows Build ====

# Reconfigure for Windows build
./Configure mingw64 no-shared no-dso no-asm no-ssl3 no-comp --cross-compile-prefix=x86_64-w64-mingw32- enable-ec_nistp_64_gcc_128 --prefix=$HOME/openssl-static/windows

# Build OpenSSL static for Windows
make -j$(nproc)
make install_sw