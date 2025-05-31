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
RELEASE_NAME="${RELEASE_NAME//./_}"
RELEASE_NAME="${RELEASE_NAME//Mercury_/}"

LINUX_ARCHIVE="../releases/Linux_$RELEASE_NAME.tar.gz"
WIN_ARCHIVE="../releases/Windows_$RELEASE_NAME.zip"

# Prepare contents
mkdir temp_release && cd temp_release

# Make logs
mkdir logs
touch logs/access.log
touch logs/error.log

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
cp ../LICENSE.txt .

# Create binary folder
mkdir bin

# ==== Linux Build ====

# Copy binary
cp ../bin/mercury bin

# Create tar.gz archive
if [ -f "$LINUX_ARCHIVE" ]; then
    rm -f $LINUX_ARCHIVE
fi
tar -czvf $LINUX_ARCHIVE * &> /dev/null

echo "✅ Linux release archive created: $LINUX_ARCHIVE"

# ==== Windows Build ====

# Replace binary
rm bin/mercury
cp ../bin/mercury.exe bin

# Create zip archive
if [ -f "$WIN_ARCHIVE" ]; then
    rm -f $WIN_ARCHIVE
fi
zip -r $WIN_ARCHIVE * &> /dev/null

echo "✅ Windows release archive created: $WIN_ARCHIVE"

# ==== Generate SHA-256 hash digests ====

echo "=================== SHA-256 ==================="
echo "  Linux: $(sha256sum $LINUX_ARCHIVE | grep -Po "[^ ]+" | head -1)"
echo "Windows: $(sha256sum $WIN_ARCHIVE | grep -Po "[^ ]+" | head -1)"

# ==== Clean Up ====

cd ../
rm -rf temp_release