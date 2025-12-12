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

if [ -d "zstd" ]; then
    if [ -t 0 ]; then
        read -r -p "This operation will overwrite an existing build of Zstandard. Continue? [y/N] " res
        res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
        if [[ "$res" =~ ^(yes|y)$ ]]; then
            rm -rf zstd
        else
            echo "Aborting..."
            exit 0
        fi
    else
        rm -rf zstd
    fi
fi

# Fetch version
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^ZSTD=)(.*)$" )

# Clean existing
$TOOLS_PATH/safe_rm "zstd-$version"
$TOOLS_PATH/safe_rm "zstd-$version-linux"
$TOOLS_PATH/safe_rm "zstd-$version-windows"
$TOOLS_PATH/safe_rm "zstd-$version.tar.gz"

# Get Zstandard source
wget -q --no-check-certificate "https://github.com/facebook/zstd/releases/download/v$version/zstd-$version.tar.gz"
echo "Fetched Zstandard archive."

mkdir zstd
mkdir zstd/linux zstd/linux/include zstd/linux/lib
if [ "$LINUX_ONLY" != "1" ]; then
    mkdir zstd/windows zstd/windows/include zstd/windows/lib
fi

# Extract archive
tar -xzf "zstd-$version.tar.gz"

# ==== Build in Parallel ====
if [ "$LINUX_ONLY" != "1" ]; then
    cp -r "zstd-$version" "zstd-$version-windows"
fi
mv "zstd-$version" "zstd-$version-linux"

# Update stored license
mv "zstd-$version-linux/LICENSE" ../licenses/Zstandard_LICENSE.txt

(
    cd "zstd-$version-linux/lib"
    make -j$(nproc) libzstd.a 1> /dev/null
    cp *.h "$LIB_PATH/zstd/linux/include"
    mv libzstd.a "$LIB_PATH/zstd/linux/lib"
) &

if [ "$LINUX_ONLY" != "1" ]; then
    (
        cd "zstd-$version-windows/lib"
        make -j$(nproc) libzstd.a CC=x86_64-w64-mingw32-gcc 1> /dev/null
        cp *.h "$LIB_PATH/zstd/windows/include"
        mv libzstd.a "$LIB_PATH/zstd/windows/lib"
    ) &
fi

wait

# ==== Clean Up ====
cd "$LIB_PATH"
rm -rf "zstd-$version"
rm -rf "zstd-$version-linux"
if [ "$LINUX_ONLY" != "1" ]; then
    rm -rf "zstd-$version-windows"
fi
rm -f "zstd-$version.tar.gz"

# Update artifacts.lock
$TOOLS_PATH/update_artifact "zstd" "$version"

echo "✅ Successfully built Zstandard v$version library."