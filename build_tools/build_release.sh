#!/bin/bash
set -e

# CD into project directory
cd "$(dirname "$0")/../"

if [ -d "temp_release" ]; then
    rm -rf temp_release
fi

# Read version
VERSION=$(cat version.txt)
RELEASE_NAME="${VERSION// /_}"
ARCHIVE_NAME="$RELEASE_NAME.tar.gz"

# Prepare contents
mkdir temp_release && cd temp_release

# Make logs
mkdir logs
touch logs/access.log
touch logs/error.log
ls -la

# Copy default config
cp -r ../conf .
cp -f ../conf/default/* conf

# Copy license, public files, & extras
cp -r ../public .
cp -r ../licenses .
cp ../version.txt .
cp ../README.md .
cp ../SECURITY.md .
cp ../CREDITS.md .
cp ../CHANGELOG.md .

# Create binary folder
mkdir bin

# ==== Linux Build ====

# Copy binary
cp ../bin/main.o bin

# Create tar.gz archive
if [ -f "../releases/$ARCHIVE_NAME" ]; then
    rm -f ../releases/$ARCHIVE_NAME
fi
tar -czvf ../releases/$ARCHIVE_NAME *

echo "✅ Linux release archive created: releases/$ARCHIVE_NAME"

# ==== Windows Build ====

ARCHIVE_NAME="$RELEASE_NAME.zip"

# Replace binary
rm bin/main.o
cp ../bin/main.exe bin

# Create zip archive
if [ -f "../releases/$ARCHIVE_NAME" ]; then
    rm -f ../releases/$ARCHIVE_NAME
fi
zip -r ../releases/$ARCHIVE_NAME *

echo "✅ Windows release archive created: releases/$ARCHIVE_NAME"

# ==== Clean Up ====

cd ../
rm -rf temp_release