#!/usr/bin/env bash

# CD into ssl directory
cd "$(dirname "$0")"

# Install openssl
sudo apt update
sudo apt install openssl -y

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