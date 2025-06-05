#!/bin/bash
set -e

# CD into project directory
cd "$(dirname "$0")/../"

# Rebuild binaries
make -B

if [ -d "temp_release" ]; then
    rm -rf temp_release
fi

# Clear releases directory
rm -rf ./releases/*

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
cp ../{version.txt,README.md,SECURITY.md,CREDITS.md,CHANGELOG.md,LICENSE.txt} .
cp -r ../{public,licenses} .

# Create binary folder
mkdir bin

# ==== Linux Build ====

# Copy binary
cp ../bin/mercury bin

# Create tar.gz archive
tar -czvf $LINUX_ARCHIVE * &> /dev/null

echo "✅ Linux release archive created: $LINUX_ARCHIVE"

# ==== Windows Build ====

# Replace binary
rm bin/mercury
cp ../bin/mercury.exe bin

# Create zip archive
zip -r $WIN_ARCHIVE * &> /dev/null

echo "✅ Windows release archive created: $WIN_ARCHIVE"

# ==== Generate SUMMARY.md ====

SUMMARY_FILE="../releases/SUMMARY_$RELEASE_NAME.md"

# Create summary file
touch $SUMMARY_FILE

# Write anchor to full changelog
echo -e "# Changelog\n### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)\n" > $SUMMARY_FILE

# Copy most recent 5 changelog entries

awk '
  /^## v[0-9]+\.[0-9]+\.[0-9]+/ {
    if (count == 5) exit
    if (entry) print entry
    entry = $0
    count++
    next
  }
  {
    if (entry) entry = entry "\n" $0
  } END {
    if (entry && count <= 5) print entry
  }
' CHANGELOG.md >> $SUMMARY_FILE

# Generate SHA-256 hash digests

echo "# SHA-256 Hashes" >> $SUMMARY_FILE
echo "| System | SHA-256 Hash Digest |" >> $SUMMARY_FILE
echo "|--------|---------------------|" >> $SUMMARY_FILE
echo -e "| Linux | \`$(sha256sum $LINUX_ARCHIVE | grep -Po "[^ ]+" | head -1)\` |" >> $SUMMARY_FILE
echo -n "| Windows | \`$(sha256sum $WIN_ARCHIVE | grep -Po "[^ ]+" | head -1)\` |" >> $SUMMARY_FILE

# ==== Clean Up ====

cd ../
rm -rf temp_release
