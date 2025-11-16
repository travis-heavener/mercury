#!/usr/bin/env bash

set -e

# CD into project directory
cd "$(dirname "$0")/../"

lockfile="/tmp/libs.lock"
exec 200>"$lockfile"
flock -x 200
if [ ! -d "libs" ]; then
    mkdir libs
fi
flock -u 200

TOOLS_PATH=$(pwd)/build_tools/tools
cd libs
LIB_PATH=$(pwd)

# Clean existing
if [ -d "openssl" ]; then
    if [ -t 0 ]; then
        read -r -p "This operation will overwrite an existing build of OpenSSL. Continue? [y/N] " res
        res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
        if [[ "$res" =~ ^(yes|y)$ ]]; then
            rm -rf openssl
        else
            echo "Aborting..."
            exit 0
        fi
    else
        # No shell connected
        rm -rf openssl
    fi
fi

# Fetch version
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^OPENSSL=)(.*)$" )

# Clean existing
$TOOLS_PATH/safe_rm "openssl-$version"
$TOOLS_PATH/safe_rm "openssl-$version-linux"
$TOOLS_PATH/safe_rm "openssl-$version-windows"
$TOOLS_PATH/safe_rm "openssl-$version.tar.gz"

# Get OpenSSL source
wget -q --no-check-certificate "https://www.openssl.org/source/openssl-$version.tar.gz"
echo "Fetched OpenSSL archive."

mkdir openssl
mkdir openssl/linux openssl/linux/include openssl/linux/lib
if [ "$LINUX_ONLY" != "1" ]; then
    mkdir openssl/windows openssl/windows/include openssl/windows/lib
fi

# Extract archive
tar -xzf "openssl-$version.tar.gz"
echo "Building static libraries, please wait..."

# ==== Build in Parallel ====
if [ "$LINUX_ONLY" != "1" ]; then
    cp -r "openssl-$version" "openssl-$version-windows"
fi
mv "openssl-$version" "openssl-$version-linux"

(
    cd "openssl-$version-linux"

    ./Configure linux-x86_64 no-shared no-dso no-asm no-ssl3 no-comp no-tests no-docs no-legacy --prefix="$LIB_PATH/openssl/linux" 1> /dev/null
    make -j$(nproc) &>/dev/null
    make install_sw &>/dev/null

    # Move library files
    mv "$LIB_PATH/openssl/linux/lib64/"*.a "$LIB_PATH/openssl/linux/lib"
    rm -rf "$LIB_PATH/openssl/linux/lib64"
    rm -rf "$LIB_PATH/openssl/linux/bin"
) &

if [ "$LINUX_ONLY" != "1" ]; then
    (
        cd "openssl-$version-windows"

        # Patch Mingw bug for 3.6.0
        # See https://github.com/openssl/openssl/issues/28679
        if [ "$version" == "3.6.0" ]; then
            sed -i '545c#if defined\(OPENSSL_SYS_WINDOWS\) && \!defined\(__MINGW32__\)' test/bioprinttest.c
        fi

        ./Configure mingw64 no-shared no-dso no-asm no-ssl3 no-comp no-tests no-docs no-legacy --cross-compile-prefix=x86_64-w64-mingw32- enable-ec_nistp_64_gcc_128 --prefix="$LIB_PATH/openssl/windows" 1> /dev/null
        make -j$(nproc) &>/dev/null
        make install_sw &>/dev/null

        # Move library files
        mv "$LIB_PATH/openssl/windows/lib64/"*.a "$LIB_PATH/openssl/windows/lib"
        rm -rf "$LIB_PATH/openssl/windows/lib64"
        rm -rf "$LIB_PATH/openssl/windows/bin"
    ) &
fi

wait

# ==== Clean Up ====
rm -f "openssl-$version.tar.gz"
rm -rf "openssl-$version-linux"
if [ "$LINUX_ONLY" != "1" ]; then
    rm -rf "openssl-$version-windows"
fi

# Update artifacts.lock
$TOOLS_PATH/update_artifact "openssl" "$version"

echo "✅ Successfully built OpenSSL v$version library."