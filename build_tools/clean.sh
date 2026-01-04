#!/usr/bin/env bash

# Cleans and initializes the repository

# CD into project directory
cd "$(dirname "$0")/../"

# Confirm execution
read -r -p "This operation will overwrite any config files, logs, libraries, builds, and releases. Continue? [y/N] " res
res=$(echo $res | tr '[:upper:]' '[:lower:]') # Lowercase
if [[ ! "$res" =~ ^(yes|y)$ ]]; then
    echo "Aborting..."
    exit 0
fi

# Clean directories
rm -f bin/* tmp/* releases/* conf/ssl/*.pem
rm -rf libs

# Reset config files
cp -f conf/default/* conf
mkdir -p logs bin libs
find ./build_tools -type f -name "*.sh" -exec chmod +x {} \;
find ./docker -type f -name "*.sh" -exec chmod +x {} \;
find ./conf/ -type f -name "*.sh" -exec chmod +x {} \;
echo -n "" > logs/access.log
echo -n "" > logs/error.log
echo -n "" > libs/artifacts.lock