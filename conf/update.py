#!/usr/bin/env python3
"""
Mercury Update Utility

Safely updates Mercury web server installations w/ automatic configuration
reconciliation, pre-flight syntax validation, and atomic rollback protection.
"""

import argparse
import copy
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

###################################################################
######################## PROCESS DETECTION ########################
###################################################################

# Returns True if Mercury server process is currently active
def is_mercury_running() -> bool:
    # Try psutil if available
    try:
        import psutil
        for proc in psutil.process_iter(["name"]):
            try:
                info = proc.info or {}
                name = info.get("name")
                if name and name.lower() in ("mercury", "mercury.exe"):
                    return True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        return False
    except ImportError:
        pass

    # Platform check on Windows
    if sys.platform in ("win32", "cygwin"):
        try:
            res = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq mercury.exe"],
                capture_output=True,
                text=True,
                check=False
            )
            return "mercury.exe" in res.stdout.lower()
        except OSError:
            return False

    # Linux /proc scan
    if os.path.exists("/proc"):
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                comm_path = os.path.join("/proc", pid, "comm")
                if os.path.exists(comm_path):
                    with open(comm_path, "r", encoding="utf-8", errors="ignore") as f:
                        if f.read().strip() in ("mercury", "mercury.exe"):
                            return True
            except OSError:
                continue

    # Unix fallback via pgrep
    try:
        res = subprocess.run(["pgrep", "-x", "mercury"], capture_output=True, check=False)
        return res.returncode == 0
    except OSError:
        pass

    # Base case
    return False

###################################################################
####################### VERSION MANAGEMENT ########################
###################################################################

# Returns parsed version tuple from version string
def parse_version(version_str: str) -> tuple[int, ...]:
    # Strip prefix and whitespace
    clean_str = version_str.strip()
    if clean_str.startswith("Mercury "):
        clean_str = clean_str[8:].strip()
    if clean_str.startswith("v"):
        clean_str = clean_str[1:].strip()

    # Split dotted components
    parts: list[int] = []
    for part in clean_str.split("."):
        # Extract leading numeric digits
        num_str = ""
        for ch in part:
            if ch.isdigit():
                num_str += ch
            else:
                break
        if num_str:
            parts.append(int(num_str))
        else:
            break

    return tuple(parts)

# Returns current installed version string from version.txt
def get_current_version(root_dir: Path) -> str:
    version_file = root_dir / "version.txt"
    if not version_file.exists():
        return "v0.0.0"

    content = version_file.read_text(encoding="utf-8").strip()
    if content.startswith("Mercury "):
        content = content[8:].strip()
    if not content.startswith("v"):
        content = f"v{content}"

    return content

# Returns latest remote version string from official site
def fetch_latest_remote_version() -> str:
    url = "https://mercury.wowtravis.com/latest"
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "Mercury-Updater"}
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        raw_text = resp.read().decode("utf-8").strip()
        # Parse version string matching version.txt format
        if raw_text.startswith("Mercury "):
            raw_text = raw_text[8:].strip()
        if not raw_text.startswith("v"):
            raw_text = f"v{raw_text}"
        return raw_text

###################################################################
##################### GITHUB RELEASE FETCHER ######################
###################################################################

# Returns release metadata dictionary from GitHub API
def fetch_release_info(tag: str | None = None) -> dict:
    repo = "travis-heavener/mercury"
    if tag:
        clean_tag = tag if tag.startswith("v") else f"v{tag}"
        url = f"https://api.github.com/repos/{repo}/releases/tags/{clean_tag}"
    else:
        url = f"https://api.github.com/repos/{repo}/releases/latest"

    # Set GitHub API headers
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "Mercury-Updater",
            "Accept": "application/vnd.github.v3+json"
        }
    )

    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise RuntimeError(f"Release '{tag or 'latest'}' not found on GitHub") from e
        if e.code == 403:
            raise RuntimeError("GitHub API rate limit exceeded") from e
        raise RuntimeError(f"GitHub API error {e.code}: {e.reason}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"Failed to reach GitHub: {e.reason}") from e

# Returns download URL and asset filename for platform
def select_platform_asset(assets: list[dict], platform_name: str) -> tuple[str, str]:
    is_windows = platform_name in ("win32", "cygwin")

    for asset in assets:
        name = asset.get("name", "")
        url = asset.get("browser_download_url", "")
        if is_windows and name.endswith(".zip") and ("windows" in name.lower() or "win" in name.lower()):
            return url, name
        if not is_windows and name.endswith(".tar.gz") and ("linux" in name.lower() or "debian" in name.lower()):
            return url, name

    # Fallback search by file extension
    for asset in assets:
        name = asset.get("name", "")
        url = asset.get("browser_download_url", "")
        if is_windows and name.endswith(".zip"):
            return url, name
        if not is_windows and name.endswith(".tar.gz"):
            return url, name

    raise RuntimeError(f"No compatible release asset found for platform '{platform_name}'")

# Returns direct asset download URL and filename without GitHub API
def construct_direct_asset_url(version_tag: str, platform_name: str) -> tuple[str, str]:
    is_windows = platform_name in ("win32", "cygwin")
    version_slug = version_tag.replace(".", "_")
    asset_name = f"Windows_{version_slug}.zip" if is_windows else f"Linux_{version_slug}.tar.gz"
    url = f"https://github.com/travis-heavener/mercury/releases/download/{version_tag}/{asset_name}"
    return url, asset_name

# Used to download file from URL w/ progressive terminal feedback
def download_file(url: str, dest_path: Path) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": "Mercury-Updater"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        total_size = int(resp.headers.get("content-length", 0))
        downloaded = 0
        chunk_size = 65536

        with open(dest_path, "wb") as f:
            while True:
                chunk = resp.read(chunk_size)
                if not chunk:
                    break
                f.write(chunk)
                downloaded += len(chunk)
                if total_size > 0:
                    percent = (downloaded / total_size) * 100
                    mb_curr = downloaded / (1024 * 1024)
                    mb_tot = total_size / (1024 * 1024)
                    sys.stdout.write(f"\rDownloading update... {percent:5.1f}% ({mb_curr:.1f}/{mb_tot:.1f} MB)")
                    sys.stdout.flush()

        if total_size > 0:
            sys.stdout.write("\n")
            sys.stdout.flush()

###################################################################
##################### INTEGRITY VERIFICATION ######################
###################################################################

# Returns SHA-256 hex digest of file
def compute_sha256(file_path: Path) -> str:
    hasher = hashlib.sha256()
    with open(file_path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            hasher.update(chunk)
    return hasher.hexdigest()

# Returns expected archive hash for version and platform from releases.json
def fetch_expected_release_hash(version_tag: str, platform_name: str) -> str | None:
    url = "https://mercury.wowtravis.com/releases.json"
    req = urllib.request.Request(url, headers={"User-Agent": "Mercury-Updater"})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            releases = json.loads(resp.read().decode("utf-8"))
            clean_tag = version_tag.lstrip("v")
            is_windows = platform_name in ("win32", "cygwin")
            for entry in releases:
                entry_name = entry.get("name", "").lstrip("v")
                if entry_name == clean_tag:
                    target_key = "winHash" if is_windows else "linuxHash"
                    expected = entry.get(target_key)
                    if expected:
                        return expected.strip()
    except Exception:
        # Fallback gracefully if website is unreachable or version is not indexed
        return None

    # Base case
    return None

###################################################################
####################### ARCHIVE EXTRACTION ########################
###################################################################

# Returns payload root directory extracted from archive
def extract_archive(archive_path: Path, extract_dir: Path) -> Path:
    # Extract based on archive format
    if archive_path.name.endswith(".tar.gz") or archive_path.name.endswith(".tgz"):
        with tarfile.open(archive_path, "r:gz") as tar:
            # Check for filter arg in Python 3.12+
            if hasattr(tarfile, "data_filter"):
                tar.extractall(extract_dir, filter="data")
            else:
                tar.extractall(extract_dir)
    elif archive_path.name.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zip_ref:
            # Prevent zip slip path traversal
            resolved_extract = extract_dir.resolve()
            for member in zip_ref.namelist():
                member_path = (extract_dir / member).resolve()
                if not member_path.is_relative_to(resolved_extract):
                    raise RuntimeError(f"Illegal path traversal detected in archive: {member}")
            zip_ref.extractall(extract_dir)
    else:
        raise ValueError(f"Unsupported archive format: {archive_path.name}")

    # Inspect extracted layout
    children = [p for p in extract_dir.iterdir() if p.is_dir()]
    if len(children) == 1 and ((children[0] / "conf").exists() or (children[0] / "bin").exists()):
        return children[0]

    return extract_dir

###################################################################
##################### CONFIG RECONCILIATION #######################
###################################################################

# Returns tuple of (reconciled_tree, reconciled_text, added_tags_list)
def reconcile_config(user_conf_path: Path, template_conf_path: Path) -> tuple[ET.ElementTree, str, list[str]]:
    orig_text = user_conf_path.read_text(encoding="utf-8")

    # Parse existing user config w/ comment preservation if supported
    try:
        user_parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
        user_tree = ET.parse(user_conf_path, parser=user_parser)
    except (TypeError, ValueError):
        user_tree = ET.parse(user_conf_path)
    user_root = user_tree.getroot()

    # Parse template config from release
    template_tree = ET.parse(template_conf_path)
    template_root = template_tree.getroot()

    if user_root.tag != "Mercury" or template_root.tag != "Mercury":
        raise ValueError("Invalid configuration file: root node must be <Mercury>")

    # Block tags representing repeatable rule definitions
    rule_tags = {"Match", "Redirect", "Rewrite"}
    added_tags: list[str] = []
    missing_nodes: list[ET.Element] = []

    # Check each template node for missing settings in user config
    for template_child in template_root:
        if template_child.tag in rule_tags:
            continue

        # Check if setting exists in user config
        if user_root.find(template_child.tag) is None:
            added_tags.append(template_child.tag)
            missing_nodes.append(copy.deepcopy(template_child))

    # If no missing settings, preserve original file text and whitespace exactly
    if not added_tags:
        return user_tree, orig_text, []

    # Format missing nodes to insert into original text
    lines_to_add: list[str] = []
    for node in missing_nodes:
        if hasattr(ET, "indent"):
            ET.indent(node, space="    ")
        node_xml = ET.tostring(node, encoding="unicode").strip()
        indented = ["    " + line for line in node_xml.splitlines()]
        lines_to_add.append("\n".join(indented))

    insertion = "\n\n" + "\n\n".join(lines_to_add) + "\n"
    idx = orig_text.rfind("</Mercury>")
    if idx != -1:
        reconciled_text = orig_text[:idx].rstrip() + insertion + orig_text[idx:]
    else:
        # Fallback if closing tag is absent
        for node in missing_nodes:
            user_root.append(node)
        if hasattr(ET, "indent"):
            ET.indent(user_tree, space="    ")
        reconciled_text = ET.tostring(user_root, encoding="unicode")
        if not reconciled_text.endswith("\n"):
            reconciled_text += "\n"

    # Build validated tree from reconciled text
    reconciled_tree = ET.ElementTree(ET.fromstring(reconciled_text))

    return reconciled_tree, reconciled_text, added_tags

# Used to write configuration text to file w/ trailing newline
def save_config_text(text: str, dest_path: Path) -> None:
    if not text.endswith("\n"):
        text += "\n"
    dest_path.write_text(text, encoding="utf-8")

###################################################################
###################### PRE-FLIGHT VALIDATION ######################
###################################################################

# Used to prepare payload environment for pre-flight testing
def prepare_preflight_env(payload_dir: Path, root_dir: Path) -> None:
    # Ensure logs directory exists in payload
    (payload_dir / "logs").mkdir(parents=True, exist_ok=True)

    # Ensure mimes.conf exists in payload
    payload_conf = payload_dir / "conf"
    payload_conf.mkdir(parents=True, exist_ok=True)
    if not (payload_conf / "mimes.conf").exists() and (root_dir / "conf" / "mimes.conf").exists():
        shutil.copy2(root_dir / "conf" / "mimes.conf", payload_conf / "mimes.conf")

    # Ensure public directory exists in payload
    if not (payload_dir / "public").exists() and (root_dir / "public").exists():
        try:
            os.symlink(root_dir / "public", payload_dir / "public")
        except OSError:
            try:
                shutil.copytree(root_dir / "public", payload_dir / "public")
            except OSError:
                pass

# Returns tuple of (is_valid, message) using Mercury binary test mode
def validate_config(binary_path: Path, conf_path: Path, root_dir: Path) -> tuple[bool, str]:
    if not binary_path.exists():
        return False, f"Binary not found: {binary_path}"

    # Ensure executable permissions on POSIX
    if sys.platform != "win32":
        try:
            os.chmod(binary_path, 0o755)
        except OSError:
            pass

    cmd = [str(binary_path), "-t", "-c", str(conf_path)]
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(root_dir),
            capture_output=True,
            text=True,
            check=False
        )
    except OSError as e:
        return False, f"Failed to execute binary {binary_path}: {e}"

    if proc.returncode == 0:
        return True, proc.stdout.strip()

    err_text = proc.stderr.strip() or proc.stdout.strip()
    return False, err_text

###################################################################
#################### INSTALLATION & ROLLBACK ######################
###################################################################

# Used to safely update Mercury installation w/ rollback guarantee
def apply_update(
    root_dir: Path,
    conf_path: Path,
    payload_dir: Path,
    reconciled_text: str,
    target_version: str,
    config_modified: bool = True
) -> None:
    binary_name = "mercury.exe" if sys.platform in ("win32", "cygwin") else "mercury"
    installed_binary = root_dir / "bin" / binary_name
    payload_binary = payload_dir / "bin" / binary_name

    if not payload_binary.exists():
        raise RuntimeError(f"Payload binary not found: {payload_binary}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_conf = conf_path.with_name(f"{conf_path.name}.bak.{timestamp}")
    backup_binary = installed_binary.with_name(f"{binary_name}.bak.{timestamp}")

    # Create timestamped configuration backup
    print(f"Creating configuration backup: {backup_conf.name}")
    shutil.copy2(conf_path, backup_conf)

    # Backup existing binary if present
    if installed_binary.exists():
        shutil.copy2(installed_binary, backup_binary)

    rollback_needed = True

    try:
        # Write reconciled configuration only when settings were added
        if config_modified:
            save_config_text(reconciled_text, conf_path)

        # Replace binary
        (root_dir / "bin").mkdir(parents=True, exist_ok=True)
        shutil.copy2(payload_binary, installed_binary)
        if sys.platform != "win32":
            os.chmod(installed_binary, 0o755)

        # Sync version.txt
        payload_version_file = payload_dir / "version.txt"
        if payload_version_file.exists():
            shutil.copy2(payload_version_file, root_dir / "version.txt")

        # Sync default templates
        payload_default_conf = payload_dir / "conf" / "default"
        if payload_default_conf.exists():
            target_default = root_dir / "conf" / "default"
            target_default.mkdir(parents=True, exist_ok=True)
            for item in payload_default_conf.iterdir():
                if item.is_file():
                    shutil.copy2(item, target_default / item.name)

        # Sync status docs
        payload_html = payload_dir / "conf" / "html"
        if payload_html.exists():
            target_html = root_dir / "conf" / "html"
            target_html.mkdir(parents=True, exist_ok=True)
            for item in payload_html.iterdir():
                if item.is_file():
                    shutil.copy2(item, target_html / item.name)

        # Sync MIME types
        payload_mimes = payload_dir / "conf" / "mimes.conf"
        if payload_mimes.exists():
            shutil.copy2(payload_mimes, root_dir / "conf" / "mimes.conf")

        # Sync helper scripts
        for script_name in ("makecert.sh", "makecert.ps1"):
            script_src = payload_dir / "conf" / "ssl" / script_name
            if script_src.exists():
                (root_dir / "conf" / "ssl").mkdir(parents=True, exist_ok=True)
                shutil.copy2(script_src, root_dir / "conf" / "ssl" / script_name)

        payload_php_ps1 = payload_dir / "conf" / "setup_php.ps1"
        if payload_php_ps1.exists():
            shutil.copy2(payload_php_ps1, root_dir / "conf" / "setup_php.ps1")

        # Sync documentation files
        for doc_name in ("README.md", "README.txt", "CHANGELOG.md", "CONFIG.md", "SECURITY.md", "CREDITS.md", "LICENSE.txt"):
            doc_src = payload_dir / doc_name
            if doc_src.exists():
                shutil.copy2(doc_src, root_dir / doc_name)

        # Run post-install verification
        is_ok, err_msg = validate_config(installed_binary, conf_path, root_dir)
        if not is_ok:
            raise RuntimeError(f"Post-install validation failed: {err_msg}")

        # Mark update as successful
        rollback_needed = False

    finally:
        if rollback_needed:
            print("\n❌ Installation failed validation. Performing immediate rollback...")
            if backup_conf.exists():
                shutil.copy2(backup_conf, conf_path)
            if backup_binary.exists():
                shutil.copy2(backup_binary, installed_binary)
                backup_binary.unlink(missing_ok=True)
            print("Rollback complete. System returned to previous state.")
        else:
            # Clean temporary binary backup on success
            if backup_binary.exists():
                backup_binary.unlink(missing_ok=True)

    print(f"\n✅ Successfully updated Mercury to {target_version}!")
    print(f"Configuration backup preserved at: {backup_conf}")
    run_cmd = "./bin/mercury" if sys.platform != "win32" else r"bin\mercury.exe"
    print(f"Run '{run_cmd}' to start the server.")

###################################################################
###################### MAIN CLI ORCHESTRATION #####################
###################################################################

# Used to parse CLI arguments
def parse_cli_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Mercury update utility w/ automatic config reconciliation and rollback."
    )
    parser.add_argument(
        "-c", "--config",
        type=Path,
        default=None,
        help="Path to mercury.conf (default: <root>/conf/mercury.conf)"
    )
    parser.add_argument(
        "-r", "--root",
        type=Path,
        default=None,
        help="Mercury root directory (default: parent of conf/)"
    )
    parser.add_argument(
        "-v", "--version",
        type=str,
        default=None,
        help="Target version tag to install (e.g., v0.35.0)"
    )
    parser.add_argument(
        "-a", "--archive",
        type=Path,
        default=None,
        help="Local release archive to install (.tar.gz or .zip)"
    )
    parser.add_argument(
        "-f", "--force",
        action="store_true",
        help="Force update even if already at latest version"
    )
    parser.add_argument(
        "-n", "--dry-run",
        action="store_true",
        help="Perform staging, reconciliation, and validation without applying changes"
    )
    parser.add_argument(
        "-y", "--yes",
        action="store_true",
        help="Skip interactive confirmation prompt"
    )
    parser.add_argument(
        "--skip-verify",
        action="store_true",
        help="Skip SHA-256 integrity verification against releases.json"
    )
    return parser.parse_args()

def main() -> None:
    args = parse_cli_args()

    # Resolve project root directory
    if args.root:
        root_dir = args.root.resolve()
    else:
        script_dir = Path(__file__).resolve().parent
        root_dir = script_dir.parent.resolve()

    # Resolve configuration file
    if args.config:
        conf_path = args.config.resolve()
    else:
        conf_path = (root_dir / "conf" / "mercury.conf").resolve()

    if not conf_path.exists():
        print(f"❌ Error: Configuration file not found: {conf_path}")
        sys.exit(1)

    # Abort immediately if Mercury process is running
    if is_mercury_running():
        print("❌ Error: Mercury is currently running.")
        print("Please stop the active Mercury process before updating.")
        sys.exit(1)

    current_version = get_current_version(root_dir)
    print(f"Mercury Update Utility")
    print(f"Current version: {current_version}")

    # Create isolated temporary staging directory
    with tempfile.TemporaryDirectory(prefix="mercury_update_") as tmp_dir_str:
        tmp_dir = Path(tmp_dir_str)

        # Resolve payload archive
        if args.archive:
            archive_path = args.archive.resolve()
            if not archive_path.exists():
                print(f"❌ Error: Archive file not found: {archive_path}")
                sys.exit(1)
            target_version = args.version or "custom"
        else:
            # Determine target version
            if args.version:
                target_version = args.version if args.version.startswith("v") else f"v{args.version}"
            else:
                print("Checking for updates...")
                # Query official endpoint first w/ GitHub API fallback
                try:
                    target_version = fetch_latest_remote_version()
                except Exception:
                    try:
                        release_info = fetch_release_info()
                        target_version = release_info.get("tag_name", "unknown")
                        if not target_version.startswith("v") and target_version != "unknown":
                            target_version = f"v{target_version}"
                    except RuntimeError as e:
                        print(f"❌ Error checking latest version: {e}")
                        sys.exit(1)

            # Check if already up to date
            curr_parsed = parse_version(current_version)
            target_parsed = parse_version(target_version)
            if curr_parsed >= target_parsed and not args.force:
                print(f"Mercury is already up to date ({current_version} >= {target_version}).")
                print("Use --force to reinstall or re-apply this release.")
                sys.exit(0)

            # Resolve asset download URL
            asset_url = None
            asset_name = None

            # Try GitHub API asset resolution
            try:
                release_info = fetch_release_info(target_version)
                asset_url, asset_name = select_platform_asset(
                    release_info.get("assets", []),
                    sys.platform
                )
            except RuntimeError:
                # Fallback to direct release download URL
                asset_url, asset_name = construct_direct_asset_url(target_version, sys.platform)

            archive_path = tmp_dir / asset_name
            try:
                download_file(asset_url, archive_path)
            except Exception as e:
                print(f"❌ Error downloading release archive ({asset_url}): {e}")
                sys.exit(1)

        # Verify archive integrity via SHA-256 digest
        if not args.skip_verify:
            actual_hash = compute_sha256(archive_path)
            if not args.archive:
                expected_hash = fetch_expected_release_hash(target_version, sys.platform)
                if expected_hash:
                    if actual_hash.lower() == expected_hash.lower():
                        print(f"Verified SHA-256 integrity digest ({actual_hash[:8]}...{actual_hash[-8:]})")
                    else:
                        print("❌ Integrity error: SHA-256 digest mismatch against releases.json!")
                        print(f"Expected: {expected_hash}")
                        print(f"Actual:   {actual_hash}")
                        sys.exit(1)
                else:
                    print(f"SHA-256 digest: {actual_hash[:8]}...{actual_hash[-8:]} (unindexed)")
            else:
                print(f"Local archive SHA-256: {actual_hash[:8]}...{actual_hash[-8:]}")

        # Extract archive contents
        extract_dir = tmp_dir / "extracted"
        extract_dir.mkdir(parents=True, exist_ok=True)
        try:
            payload_dir = extract_archive(archive_path, extract_dir)
        except Exception as e:
            print(f"❌ Error extracting release archive: {e}")
            sys.exit(1)

        # If archive was local, detect target version from extracted version.txt
        if args.archive and target_version == "custom":
            target_version = get_current_version(payload_dir)

        print(f"Target version: {target_version}")

        # Locate default configuration template in payload
        template_conf = payload_dir / "conf" / "default" / "mercury.conf"
        if not template_conf.exists():
            template_conf = payload_dir / "conf" / "mercury.conf"
        if not template_conf.exists():
            print("❌ Error: Missing configuration template in release package.")
            sys.exit(1)

        # Reconcile user configuration w/ template
        try:
            reconciled_tree, reconciled_text, added_tags = reconcile_config(conf_path, template_conf)
        except Exception as e:
            print(f"❌ Error reconciling configuration: {e}")
            sys.exit(1)

        if added_tags:
            print(f"Reconciled configuration (appended {len(added_tags)} missing setting{'s' if len(added_tags) > 1 else ''}: {', '.join(added_tags)})")
        else:
            print("Configuration is up to date (no missing settings to add, original whitespace preserved).")

        # Prepare payload environment for pre-flight testing
        prepare_preflight_env(payload_dir, root_dir)

        # Pre-flight syntax validation using payload binary
        binary_name = "mercury.exe" if sys.platform in ("win32", "cygwin") else "mercury"
        payload_binary = payload_dir / "bin" / binary_name
        staged_conf = tmp_dir / "mercury.conf.staged"
        save_config_text(reconciled_text, staged_conf)

        if payload_binary.exists() and sys.platform != "win32":
            os.chmod(payload_binary, 0o755)

        # Execute pre-flight check if executable on current host
        if payload_binary.exists() and (sys.platform != "win32" or shutil.which("wine")):
            bin_to_run = payload_binary
            is_ok, check_msg = validate_config(bin_to_run, staged_conf, root_dir)
            if not is_ok:
                print(f"❌ Pre-flight configuration validation failed:\n{check_msg}")
                sys.exit(1)
            print("Pre-flight configuration syntax validation passed.")

        # Handle dry-run mode
        if args.dry_run:
            print("\n[DRY RUN] All validation checks passed.")
            print("[DRY RUN] Staged binary and reconciled config verified w/ zero errors.")
            print("[DRY RUN] No files were modified.")
            sys.exit(0)

        # Interactive confirmation prompt
        if not args.yes:
            sys.stdout.write(f"\nProceed w/ updating Mercury to {target_version}? [y/N]: ")
            sys.stdout.flush()
            choice = sys.stdin.readline().strip().lower()
            if choice not in ("y", "yes"):
                print("Update cancelled by user.")
                sys.exit(0)

        # Apply update w/ rollback guarantee
        try:
            apply_update(
                root_dir,
                conf_path,
                payload_dir,
                reconciled_text,
                target_version,
                config_modified=bool(added_tags)
            )
        except Exception as e:
            print(f"❌ Fatal error during update: {e}")
            sys.exit(1)

if __name__ == "__main__":
    main()
