#!/bin/bash

set -e

# CD into project directory
cd "$(dirname "$0")/../"

if [ ! -d "libs" ]; then
    mkdir libs
fi

TOOLS_PATH=$(pwd)/build_tools/tools
cd libs
LIB_PATH=$(pwd)

# Clean existing
if [ -d "pugixml" ]; then
    read -r -p "This operation will overwrite an existing build of PugiXML. Continue? [y/N] " res
    res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
    if [[ "$res" =~ ^(yes|y)$ ]]; then
        rm -rf pugixml
    else
        echo "Aborting..."
        exit 0
    fi
fi

# Fetch version
version=$( cat ../build_tools/dependencies.txt | grep -Po "(?<=^PUGIXML=)(.*)$" )

# Clean existing
$TOOLS_PATH/safe_rm "pugixml-$version"
$TOOLS_PATH/safe_rm "pugixml-$version.tar.gz"

# ==== Download ====
wget -q --no-check-certificate "https://github.com/zeux/pugixml/releases/download/v$version/pugixml-$version.tar.gz"
tar -xzf "pugixml-$version.tar.gz"

# Move source
mkdir pugixml
mv "pugixml-$version/src/"* pugixml

# ==== Clean Up ====
cd "$LIB_PATH"
rm -rf "pugixml-$version"
rm -f "pugixml-$version.tar.gz"

# Update artifacts.lock
$TOOLS_PATH/update_artifact "pugixml" "$version"

echo "✅ Successfully fetched PugiXML v$version source."