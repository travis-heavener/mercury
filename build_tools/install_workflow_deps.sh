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

# Bound stalled connections and package-manager lock waits
apt_opts=(
    -o Acquire::Retries=1
    -o Acquire::http::Timeout=30
    -o Acquire::https::Timeout=30
    -o DPkg::Lock::Timeout=120
)

# Used to bound network phases without interrupting package installation
run_fetch() {
    local phase="$1" limit="$2"
    shift 2

    echo "$phase..." >&3
    if sudo timeout --kill-after=10s "$limit" "$@" >&3; then
        return 0
    else
        local status=$?
        if [[ "$status" == 124 || "$status" == 137 ]]; then
            echo "$phase timed out after $limit. Check APT mirror connectivity and retry." >&2
        else
            echo "$phase failed (exit $status). See APT errors above." >&2
        fi
        return "$status"
    fi
}

# Refresh indexes without silently accepting failed repository downloads
run_fetch "Updating package indexes" 180s \
    apt-get "${apt_opts[@]}" -o APT::Update::Error-Mode=any update

# Download packages before allowing dpkg to modify installed files
run_fetch "Downloading workflow dependencies" 600s \
    env DEBIAN_FRONTEND=noninteractive apt-get "${apt_opts[@]}" install \
    mingw-w64 -y --no-install-recommends --download-only

# Install downloaded packages without applying network-phase deadlines to dpkg
echo "Installing workflow dependencies..." >&3
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${apt_opts[@]}" install \
    mingw-w64 -y --no-install-recommends --no-download >&3

echo "✅ Successfully installed workflow dependencies."
