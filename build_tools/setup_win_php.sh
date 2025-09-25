#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ -d "php" ]; then
    rm -rf php
fi

# Download zip archive
DOWNLOAD_URL="https://downloads.php.net/~windows/releases/php-8.4.12-Win32-vs17-x64.zip"
wget -q --no-check-certificate "$DOWNLOAD_URL"

# Unzip the archive
unzip -q "php-8.4.12-Win32-vs17-x64.zip" -d "php"
rm -f "php-8.4.12-Win32-vs17-x64.zip"