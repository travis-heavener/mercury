#!/usr/bin/env bash

if ! command -v docker >/dev/null 2>&1; then
    echo "Failed to run Docker tests: Docker is not installed"
    exit 1
fi

# CD into Mercury directory
cd "$(dirname "$0")/../"

# Grab current GLIBC version
glibc=$(ldd --version | grep -Po "\d+\.\d+$")

# Initialize Docker
distros=(
    ubuntu:24.04
    debian:13
    archlinux:latest
    kalilinux/kali-rolling
    fedora:43
    rockylinux:9 # CentOS 8+ & REHL replacement
)

results="┌─────────────────────────────────────┐"

for img in "${distros[@]}"; do
    tag=${img//[:\/\.]/-}
    echo "Starting tests on $tag"

    # Create the container
    sudo docker build \
        --build-arg BASE_IMAGE="$img" \
        -t "test-$tag" \
        -q \
        . 1>/dev/null

    # Run the Docker container
    sudo docker run --rm \
        -v "$(pwd)":/workspace \
        -e HOST_GLIBC="$glibc" \
        --network=host \
        "test-$tag"

    if [ $? -ne 0 ]; then
        printf -v results "%s\n│ %24s : %-8s │" "$results" "$tag" "Failed"
    else
        printf -v results "%s\n│ %24s : %-8s │" "$results" "$tag" "Success"
    fi
    echo "================================================================"
done

# Print results
printf -v results "%s\n└─────────────────────────────────────┘\n" "$results"
printf "$results"

# Handle exit status
if printf "$results" | grep -Po "Failed"; then
    exit 1
else
    exit 0
fi
