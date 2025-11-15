#!/usr/bin/env bash

# CD into ssl directory
cd "$(dirname "$0")"

# Install openssl
case "$(./tools/getpm)" in
    apt)
        sudo apt-get update 1>/dev/null
        sudo apt install openssl -y 1>/dev/null
        ;;
    pacman)
        sudo pacman -Sy --noconfirm 1>/dev/null
        sudo pacman -S --noconfirm openssl 1>/dev/null
        ;;
    # Not implemented yet
    # dnf)
    #     sudo dnf makecache -y 1>/dev/null
    #     sudo dnf install -y openssl 1>/dev/null
    #     ;;
    # yum)
    #     sudo yum makecache -y 1>/dev/null
    #     sudo yum install -y openssl 1>/dev/null
    #     ;;
    *)
        echo "Unknown package manager: $PKG_MGR" >&2
        exit 1
        ;;

if [ -f "key.pem" ]; then
    rm -f key.pem
fi

if [ -f "cert.pem" ]; then
    rm -f cert.pem
fi

# Generate cert
openssl req -x509 \
    -newkey rsa:4096 \
    -keyout key.pem \
    -out cert.pem \
    -sha256 \
    -days 365 \
    -nodes