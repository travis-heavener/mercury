#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "static_libs" ]; then
    mkdir static_libs
fi

cd static_libs
LIB_PATH=$(pwd)

# Clean existing
if [ -d "openssl-3.5.0" ]; then
    rm -rf openssl-3.5.0
fi

if [ -d "openssl" ]; then
    rm -rf openssl
fi

if [ -f "openssl-3.5.0.tar.gz" ]; then
    rm -f openssl-3.5.0.tar.gz
fi

# ==== Linux Build ====

# Get OpenSSL source
wget -q https://www.openssl.org/source/openssl-3.5.0.tar.gz

echo "Fetched OpenSSL archive."
tar -xzf openssl-3.5.0.tar.gz
echo "Extracted archive."

cd openssl-3.5.0

# Configure static build
echo "Configuring build... This may take a minute."
./Configure linux-x86_64 no-shared no-dso no-ssl3 no-comp --prefix=$LIB_PATH/openssl/linux 1> /dev/null

# Build static
make -j$(nproc) 1> /dev/null

# Install binaries
make install_sw 1> /dev/null
echo "Built Linux binaries."

# Reset for Windows build
cd ..
rm -rf openssl-3.5.0

# ==== Windows Build ====

# Unpack tar
tar -xzf openssl-3.5.0.tar.gz
echo "Extracted archive."
cd openssl-3.5.0

# Reconfigure for Windows build
echo "Configuring build... This may take a minute."
./Configure mingw64 no-shared no-dso no-asm no-ssl3 no-comp --cross-compile-prefix=x86_64-w64-mingw32- enable-ec_nistp_64_gcc_128 --prefix=$LIB_PATH/openssl/windows 1> /dev/null

# Build OpenSSL static for Windows
make -j$(nproc) 1> /dev/null
make install_sw 1> /dev/null
echo "Built Windows binaries."

# ==== Clean Up ====

rm -rf $LIB_PATH/openssl-3.5.0
rm -f $LIB_PATH/openssl-3.5.0.tar.gz

echo "✅ Successfully built static OpenSSL binaries."