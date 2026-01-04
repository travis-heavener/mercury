#!/usr/bin/env bash

# Installs *all* deps needed to rebuild Mercury
sudo apt-get update 1> /dev/null
sudo DEBIAN_FRONTEND=noninteractive apt-get install \
    mingw-w64 \
    -y --no-install-recommends 1> /dev/null

# Install Docker
# Source: https://docs.docker.com/engine/install/ubuntu/

sudo apt-get remove $(dpkg --get-selections docker.io docker-compose docker-compose-v2 docker-doc podman-docker containerd runc | cut -f1) \
    1>/dev/null

# Add Docker's official GPG key:
sudo apt-get install ca-certificates curl 1>/dev/null
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

# Add the repository to Apt sources:
sudo tee /etc/apt/sources.list.d/docker.sources <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Signed-By: /etc/apt/keyrings/docker.asc
EOF

sudo apt-get update 1>/dev/null
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin 1>/dev/null

echo "✅ Successfully installed workflow dependencies."