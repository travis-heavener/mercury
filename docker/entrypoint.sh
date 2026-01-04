#!/usr/bin/env bash
set -e

# CD into script directory
cd "$(dirname "$0")/"

# Installs Python per distro
chmod +x ../build_tools/tools/getpm
PKG_MGR=$("../build_tools/tools/getpm")

case "$PKG_MGR" in
    apt)
        DEBIAN_FRONTEND=noninteractive apt-get update 1>/dev/null
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            python3 python3-brotli python3-zstandard python3-psutil \
            php-cgi 1>/dev/null
        ;;
    pacman)
        pacman -Syu --noconfirm 1>/dev/null
        pacman -S --noconfirm \
            python python-brotli python-zstandard python-psutil \
            php-cgi 1>/dev/null
        ;;
    dnf)
        dnf makecache -y &>/dev/null
        dnf install -y \
            python3 python3-pip \
            php-cgi &>/dev/null
        python3 -m pip install brotli zstandard psutil &>/dev/null
        ;;
    *)
        echo "Unknown package manager: $PKG_MGR" >&2
        exit 1
        ;;
esac

# Run tests
set +e
output=$(python3 ../tests/run.py 2>&1)
code=$?
set -e
if [ $code -ne 0 ]; then
    printf "%s\n" "$output"
else
    echo "Success."
fi

exit 0
