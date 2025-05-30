#!/bin/bash

# CD into tests directory
cd "$(dirname "$0")"

# Iterate over each potential HTTP request over HTTP
for file in "./requests"/*; do
    # Determine the expected code
    expected_code=$(cat "./expected/requests/"$(basename $file))

    # Invoke each HTTP request
    status_code=$(cat "$file" | ncat 127.0.0.1 80 \
        | head -1 \
        | grep -Po "(?<=HTTP/1.1 )[^ ]+")

    # Check if returning invalid response
    if [[ $status_code != $expected_code ]]; then
        # Failed response
        echo "Failed ($status_code): $(basename $file)"
    fi
done