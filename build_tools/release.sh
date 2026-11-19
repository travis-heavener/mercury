#!/usr/bin/env bash
set -e

# CD into project directory
cd "$(dirname "$0")/../"
TOOLS_PATH=$(pwd)/build_tools/tools

# Select binary directories
binary_dirs=(bin)
if [ "$1" = "--ci-artifacts" ]; then
    binary_dirs=(bin/x86_64 bin/arm64)
elif [ "$1" != "--no-build" ] && [ "$SKIP_BUILD" != "1" ]; then
    make -j$(nproc) --no-print-directory -s
fi

# Verify binaries before preparing release directory
for binary_dir in "${binary_dirs[@]}"; do
    if [ ! -s "$binary_dir/mercury" ] || [ ! -s "$binary_dir/mercury.exe" ]; then
        echo "Missing release binaries in $binary_dir/" >&2
        exit 1
    fi
done

"$TOOLS_PATH/safe_rm" "temp_release"
"$TOOLS_PATH/safe_rm" "releases"

mkdir releases

# Read version
VERSION=$(cat version.txt | head -n 1 | tr -d '\n')
RELEASE_NAME="${VERSION// /_}"
RELEASE_NAME="${RELEASE_NAME//./_}"
RELEASE_NAME="${RELEASE_NAME//Mercury_/}"

LINUX_ARCHIVE="../releases/Linux_$RELEASE_NAME.tar.gz"
WIN_ARCHIVE="../releases/Windows_$RELEASE_NAME.zip"

# Preserve x64 asset names used by website, updater, and benchmarks
linux_archives=("$LINUX_ARCHIVE" "../releases/Mercury_ARM64_$RELEASE_NAME.tar.gz")
win_archives=("$WIN_ARCHIVE" "../releases/Mercury_ARM64_$RELEASE_NAME.zip")
architectures=(x86_64 ARM64)

# Prepare contents
mkdir temp_release && cd temp_release

# Create internal directory
mkdir "$VERSION"

# Make logs
mkdir "./$VERSION/logs"
touch "./$VERSION/logs/access.log"
touch "./$VERSION/logs/error.log"

# Copy default config
cp -r ../conf "./$VERSION/"
cp -f ../conf/default/* "./$VERSION/conf"
rm -rf "./$VERSION/conf/__pycache__"
find "./$VERSION/conf/ssl/" -name "*" -type f -delete

# Copy license, public files, & extras
cp ../{version.txt,README.md,SECURITY.md,CREDITS.md,CHANGELOG.md,CONFIG.md,LICENSE.txt} "./$VERSION/"
cp -r ../{public,licenses} "./$VERSION/"

# Create binary folder
mkdir "./$VERSION/bin"

# ==== Update README.md to README.txt ====

python3 ../build_tools/release_readme_maker.py "./$VERSION/README.md"
mv "./$VERSION/README.md" "./$VERSION/README.txt"

# ==== Linux Build ====

# Copy makecert
cp "../conf/ssl/makecert.sh" "./$VERSION/conf/ssl/"
rm -f "./$VERSION/conf/setup_php.ps1"

# Package Linux binaries w/ executable permissions
for i in "${!binary_dirs[@]}"; do
    archive="${linux_archives[$i]}"
    install -m 755 "../${binary_dirs[$i]}/mercury" "./$VERSION/bin/mercury"
    tar -czf "$archive" "$VERSION"
    echo "✅ Linux ${architectures[$i]} release archive created: ${archive:3}"
done

# ==== Windows Build ====

# Copy Powershell scripts
rm "./$VERSION/bin/mercury"
rm "./$VERSION/conf/ssl/makecert.sh"
cp "../conf/ssl/makecert.ps1" "./$VERSION/conf/ssl/"
cp "../conf/setup_php.ps1" "./$VERSION/conf/"

# Package Windows binaries
for i in "${!binary_dirs[@]}"; do
    archive="${win_archives[$i]}"
    cp "../${binary_dirs[$i]}/mercury.exe" "./$VERSION/bin/mercury.exe"
    zip -qr "$archive" "$VERSION"
    echo "✅ Windows ${architectures[$i]} release archive created: ${archive:3}"
done

# ==== Generate SUMMARY.md ====

SUMMARY_FILE="../releases/SUMMARY_$RELEASE_NAME.md"

# Create summary file
touch "$SUMMARY_FILE"

# Write anchor to full changelog
echo -e "# Changelog\n### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)\n" > "$SUMMARY_FILE"

# Copy most recent N changelog entries

awk '
  /^## (\[v?[0-9]+\.[0-9]+\.[0-9]+\]|v[0-9]+\.[0-9]+\.[0-9]+)/ {
    if (count == 1) exit
    if (entry) print entry
    entry = $0
    count++
    next
  }
  {
    if (entry) entry = entry "\n" $0
  } END {
    if (entry && count <= 1) print entry
  }
' "./$VERSION/CHANGELOG.md" >> "$SUMMARY_FILE"

# Generate SHA-256 hash digests

release_hashes=()
{
    printf '# SHA-256 Hashes\n\n'
    printf '| System | Archive | SHA-256 Hash Digest |\n'
    printf '|--------|---------|---------------------|\n'
    for i in "${!binary_dirs[@]}"; do
        linux_hash=$(sha256sum "${linux_archives[$i]}")
        win_hash=$(sha256sum "${win_archives[$i]}")
        printf '| Linux %s | `%s` | `%s` |\n' "${architectures[$i]}" "${linux_archives[$i]##*/}" "${linux_hash%% *}"
        printf '| Windows %s | `%s` | `%s` |\n' "${architectures[$i]}" "${win_archives[$i]##*/}" "${win_hash%% *}"
        release_hashes+=("${linux_hash%% *}" "${win_hash%% *}")
    done
} >> "$SUMMARY_FILE"

# ==== Clean Up ====

cd ../
rm -rf temp_release

# ==== Update Releases JSON File ====

python3 ./build_tools/update_releases.py "$VERSION" "${release_hashes[@]}"

# ==== Update Latest Version Placeholder ====

echo -n "$VERSION" > ./website/latest
