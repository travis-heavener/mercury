#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

cd libs
LIB_PATH=$(pwd)

# Update artifacts.lock
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^PUGIXML=)(.*)$" )
if [ ! -e "artifacts.lock" ]; then
    touch artifacts.lock
    echo "" | gzip | base64 > artifacts.lock
fi

# Unpack artifacts
cat artifacts.lock | base64 --decode | gunzip > artifacts.raw

if grep -q "^pugixml=" artifacts.raw; then
    sed -i "s/^pugixml=.*$/pugixml=$version/" artifacts.raw
else
    { echo "pugixml=$version"; cat artifacts.raw; } > temp && mv temp artifacts.raw
fi

# Repack artifacts
cat artifacts.raw | gzip | base64 > artifacts.lock
rm -f artifacts.raw

# Clean existing
if [ -d "pugixml" ]; then
    rm -rf pugixml
fi

if [ -d "pugixml-$version" ]; then
    rm -rf "pugixml-$version"
fi

if [ -f "pugixml-$version.tar.gz" ]; then
    rm -f "pugixml-$version.tar.gz"
fi

# ==== Download ====
wget -q --no-check-certificate "https://github.com/zeux/pugixml/releases/download/v$version/pugixml-$version.tar.gz"

# Extract & remove tarball
tar -xzf "pugixml-$version.tar.gz"
rm "pugixml-$version.tar.gz"

# Make lib directory
mkdir pugixml

# Move source
mv "pugixml-$version/src/"* pugixml

# ==== Clean Up ====
rm -rf "pugixml-$version"

echo "✅ Successfully fetched PugiXML v$version source."