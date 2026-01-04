#!/usr/bin/env bash

if ! command -v docker >/dev/null 2>&1; then
    echo "Failed to run Docker tests: Docker is not installed"
    exit 1
fi

# CD into Mercury directory
cd "$(dirname "$0")/../"

# Initialize Docker
distros=(
    # ubuntu:22.04
    ubuntu:24.04

    # debian:12
    debian:13

    archlinux:latest

    kalilinux/kali-rolling

    fedora:42
    fedora:43

    # centos:7 # For testing yum
    # rockylinux:9 # CentOS 8+ & REHL replacement
)

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
        --network=host \
        "test-$tag"

    echo "================================================================"
done
