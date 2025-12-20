#!/usr/bin/env bash

# CD into ssl directory
cd "$(dirname "$0")"

# Install openssl
if command -v apt >/dev/null 2>&1; then
    sudo apt-get update 1>/dev/null
    sudo apt-get install openssl -y 1>/dev/null
elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -Syu --noconfirm 1>/dev/null
    sudo pacman -S --noconfirm openssl 1>/dev/null
elif command -v dnf >/dev/null 2>&1; then
    sudo dnf makecache -y 1>/dev/null
    sudo dnf install -y openssl 1>/dev/null
fi

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