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

# Identify Azure sources, including GitHub runner mirror lists
source_files=()
for source in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    if [[ -f "$source" ]]; then source_files+=("$source"); fi
done
azure_sources=false
if (( ${#source_files[@]} > 0 )) && grep -Eq \
    'https?://azure[.]archive[.]ubuntu[.]com/ubuntu|mirror[+]file:/etc/apt/apt-mirrors[.]txt' \
    "${source_files[@]}"; then
    azure_sources=true
fi

# Remove temporary repository configuration on success or failure
fallback_dir=""
cleanup() {
    if [[ -n "$fallback_dir" ]]; then sudo rm -rf -- "$fallback_dir" || true; fi
}
trap cleanup EXIT

# Used to retry Azure failures with isolated copies of configured repositories
prepare_archive_fallback() {
    if [[ "$azure_sources" != true ]]; then return 1; fi

    fallback_dir=$(mktemp -d "${TMPDIR:-/tmp}/mercury-apt.XXXXXX") || return
    mkdir -p "$fallback_dir/sources.list.d" "$fallback_dir/lists/partial" || return
    chmod 755 "$fallback_dir" "$fallback_dir/sources.list.d" "$fallback_dir/lists" "$fallback_dir/lists/partial" || return
    touch "$fallback_dir/sources.list" || return

    for source in "${source_files[@]}"; do
        local destination="$fallback_dir/sources.list.d/${source##*/}"
        if [[ "$source" == /etc/apt/sources.list ]]; then destination="$fallback_dir/sources.list"; fi

        # Preserve suites, signing keys, permissions, and unrelated repository URLs
        sed -E \
            -e 's#https?://azure\.archive\.ubuntu\.com/ubuntu/?#https://archive.ubuntu.com/ubuntu/#g' \
            -e 's#mirror\+file:/etc/apt/apt-mirrors\.txt#https://archive.ubuntu.com/ubuntu/#g' \
            "$source" > "$destination" || return
        chmod --reference="$source" "$destination" || return
    done

    # Keep fallback sources and indexes separate from system configuration
    apt_opts+=(
        -o "Dir::Etc::sourcelist=$fallback_dir/sources.list"
        -o "Dir::Etc::sourceparts=$fallback_dir/sources.list.d"
        -o "Dir::State::lists=$fallback_dir/lists/"
    )
    echo "Retrying APT downloads using Ubuntu main archive..." >&2
}

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

# Used to fetch indexes and packages before allowing dpkg to modify files
fetch_dependencies() {
    local download_limit="$1"

    # Reject incomplete index refreshes instead of silently accepting stale lists
    run_fetch "Updating package indexes" 180s \
        apt-get "${apt_opts[@]}" -o APT::Update::Error-Mode=any update || return $?

    run_fetch "Downloading workflow dependencies" "$download_limit" \
        env DEBIAN_FRONTEND=noninteractive apt-get "${apt_opts[@]}" install \
        mingw-w64 -y --no-install-recommends --download-only || return $?
}

# Switch away from stalled Azure downloads after at most three minutes
download_limit=600s
if [[ "$azure_sources" == true ]]; then download_limit=180s; fi
if fetch_dependencies "$download_limit"; then
    :
else
    status=$?
    if ! prepare_archive_fallback; then exit "$status"; fi
    fetch_dependencies 600s
fi

# Install downloaded packages without applying network-phase deadlines to dpkg
echo "Installing workflow dependencies..." >&3
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${apt_opts[@]}" install \
    mingw-w64 -y --no-install-recommends --no-download >&3

echo "✅ Successfully installed workflow dependencies."
