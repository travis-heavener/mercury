#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

# Make bin & logs directory

mkdir -p ./bin
mkdir -p ./logs

# Flock to prevent race condition in workflow
lockfile="/tmp/mercury_conf.lock"
exec 200>"$lockfile"
flock -x 200

# Copy default config if missing

if [ ! -f ./conf/mercury.conf ]; then
    cp -f ./conf/default/mercury.conf ./conf
fi

if [ ! -f ./conf/mimes.conf ]; then
    cp -f ./conf/default/mimes.conf ./conf
fi

flock -u 200