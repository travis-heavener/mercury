#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs
LIB_PATH=$(pwd)

# Update artifacts.lock
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^OPENSSL=)(.*)$" )
if [ ! -e "artifacts.lock" ]; then
    touch artifacts.lock
    echo "" | gzip | base64 > artifacts.lock
fi

# Unpack artifacts
cat artifacts.lock | base64 --decode | gunzip > artifacts.raw

if grep -q "^openssl=" artifacts.raw; then
    sed -i "s/^openssl=.*$/openssl=$version/" artifacts.raw
else
    { echo "openssl=$version"; cat artifacts.raw; } > temp && mv temp artifacts.raw
fi

# Repack artifacts
cat artifacts.raw | gzip | base64 > artifacts.lock
rm -f artifacts.raw

# Clean existing
if [ -d "openssl" ]; then
    rm -rf openssl
fi

if [ -d "openssl-$version" ]; then
    rm -rf "openssl-$version"
fi

if [ -f "openssl-$version.tar.gz" ]; then
    rm -f "openssl-$version.tar.gz"
fi

# Get OpenSSL source
wget -q --no-check-certificate "https://www.openssl.org/source/openssl-$version.tar.gz"
echo "Fetched OpenSSL archive."

# ==== Linux Build ====

# Unpack tar
tar -xzf "openssl-$version.tar.gz"
echo "Extracted archive."
cd "openssl-$version"

# Configure static build
echo "Configuring build... This may take a minute."
./Configure linux-x86_64 no-shared no-dso no-ssl3 no-comp "--prefix=$LIB_PATH/openssl/linux" 1> /dev/null

# Move library files
mkdir "$LIB_PATH/openssl/windows/lib"
mv "$LIB_PATH/openssl/windows/lib64/*.a" "$LIB_PATH/openssl/windows/lib"
rm -rf "$LIB_PATH/openssl/windows/lib64"

# Build static
make -j$(nproc) 1> /dev/null

# Install binaries
make install_sw 1> /dev/null
echo "Built Linux binaries."

# Reset for Windows build
cd ..
rm -rf openssl-$version

# ==== Windows Build ====

# Unpack tar
tar -xzf openssl-$version.tar.gz
echo "Extracted archive."
cd openssl-$version

# Patch Mingw bug for 3.6.0
# See https://github.com/openssl/openssl/issues/28679
if [ "$version" == "3.6.0" ]; then
    sed -i '545c#if defined\(OPENSSL_SYS_WINDOWS\) && \!defined\(__MINGW32__\)' test/bioprinttest.c
fi

# Reconfigure for Windows build
echo "Configuring build... This may take a minute."
./Configure mingw64 no-shared no-dso no-asm no-ssl3 no-comp --cross-compile-prefix=x86_64-w64-mingw32- enable-ec_nistp_64_gcc_128 "--prefix=$LIB_PATH/openssl/windows" 1> /dev/null

# Move library files
mkdir "$LIB_PATH/openssl/windows/lib"
mv "$LIB_PATH/openssl/windows/lib64/*.a" "$LIB_PATH/openssl/windows/lib"
rm -rf "$LIB_PATH/openssl/windows/lib64"

# Build OpenSSL static for Windows
make -j$(nproc) 1> /dev/null
make install_sw 1> /dev/null
echo "Built Windows binaries."

cd ..
rm -rf "openssl-$version"

# ==== Clean Up ====

rm -f "$LIB_PATH/openssl-$version.tar.gz"

echo "✅ Successfully built OpenSSL v$version library."