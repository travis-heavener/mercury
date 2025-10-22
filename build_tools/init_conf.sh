#!/bin/bash

# CD into project directory
cd "$(dirname "$0")/../"

# Make bin & logs directory

mkdir -p ./bin
mkdir -p ./logs

# Copy default config if missing

if [ ! -f ./conf/mercury.conf ]; then
    cp -f ./conf/default/mercury.conf ./conf
fi

if [ ! -f ./conf/mimes.conf ]; then
    cp -f ./conf/default/mimes.conf ./conf
fi