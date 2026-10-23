#!/usr/bin/env bash

set -e

# Keep normal output quiet; expose progress when requested by CI
exec 3> /dev/null
for arg in "$@"; do
    case "$arg" in
        --verbose|-v) exec 3>&1 ;;
        *)
            echo "Usage: $0 [--verbose|-v]" >&2
            exit 2
            ;;
    esac
done

# Install workflow deps while preserving error output in both modes
echo "Updating package indexes..." >&3
sudo apt-get update >&3
echo "Installing workflow dependencies..." >&3
sudo DEBIAN_FRONTEND=noninteractive apt-get install \
    mingw-w64 \
    -y --no-install-recommends >&3

echo "✅ Successfully installed workflow dependencies."
