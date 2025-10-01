#!/bin/bash

set -euo pipefail

# Save current location
pushd "$(pwd)" >/dev/null

# Detect dev enviroment
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(basename "$SCRIPT_DIR")" == "conf" && -d "$SCRIPT_DIR/../.git" ]]; then
    echo "It appears that you're in a dev environment"
    while true; do
        read -rp "Do you want to proceed? (Y/N) " userInput
        case "$userInput" in
            [Yy]) break ;;
            [Nn]) echo "Aborting..."; popd >/dev/null; exit 1 ;;
        esac
    done
    # CD into Mercury root
    cd "$SCRIPT_DIR/.."
fi

# Confirm from user
while true; do
    echo "The following operation will overwrite any changes you've made to the following:"
    echo " - Configuration files (e.g. mercury.conf, mimes.conf)"
    echo " - Default HTML files (e.g. ./public/index.html, ./conf/html/err.html)"
    echo "SSL certs in ./conf/ssl will be saved."
    read -rp "Do you want to proceed? (Y/N) " userInput
    case "$userInput" in
        [Yy]) break ;;
        [Nn]) echo "Aborting..."; popd >/dev/null; exit 1 ;;
    esac
done

# Fetch latest version
versionURL="https://wowtravis.com/mercury/latest"
latestVersion="$(curl -sL "$versionURL" | tr -d '\r' | sed 's/^.\{9\}//')"
echo "Downloading version v$latestVersion..."

archiveDir="Mercury v$latestVersion"
rm -rf "$archiveDir"

latestVersionUnderscored="${latestVersion//./_}"
archiveURL="https://github.com/travis-heavener/mercury/releases/download/v$latestVersion/Linux_v${latestVersionUnderscored}.tar.gz"

archivePath="latest.tar.gz"
rm -f "$archivePath"
curl -L "$archiveURL" -o "$archivePath"

# Extract archive
tar -xzf "$archivePath"
rm -f "$archivePath"

#####################################################
################## BEGIN REPLACING ##################
#####################################################

# Keep SSL certs
mkdir -p tmp_ssl
mv conf/ssl/*.pem tmp_ssl/ 2>/dev/null || true

# Remove everything except archiveDir and .pem files
find . -mindepth 1 -maxdepth 1 \
    ! -name "$archiveDir" \
    ! -name 'tmp_ssl' \
    -exec rm -rf {} +

# Copy all new files except update.sh (Linux updater)
rsync -a --exclude 'update.sh' "$archiveDir"/ ./

# Restore SSL certs
mkdir -p conf/ssl
mv tmp_ssl/*.pem conf/ssl/ 2>/dev/null || true
rm -rf tmp_ssl

# Detach shell to finish updating the update script
scriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
archivePath="$(pwd)/$archiveDir"

(
    sleep 2
    if [[ -f "$archivePath/update.sh" ]]; then
        mv -f "$archivePath/update.sh" "$scriptDir/update.sh"
    fi
    rm -rf "$archivePath"
) & disown

# Restore location and exit main updater
popd >/dev/null
exit 0