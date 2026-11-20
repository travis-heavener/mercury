import json
from sys import argv
import time

def main(version_full: str, linux_hash: str, win_hash: str,
         linux_arm64_hash: str = "", win_arm64_hash: str = ""):
    version = version_full.replace("Mercury ", "")

    # Load existing JSON
    releases_json: list = None

    with open("./website/releases.json", "r") as f:
        releases_json = json.load(f)

    # Remove existing releases with matching version
    releases_json = [ r for r in releases_json if r["name"] != version ]

    # Prepend new version entry
    release = {
        "name": version,
        "date": int(round(time.time() * 1000)),
        "linuxHash": linux_hash,
        "winHash": win_hash
    }
    if linux_arm64_hash:
        release["linuxHashArm64"] = linux_arm64_hash
    if win_arm64_hash:
        release["winHashArm64"] = win_arm64_hash
    releases_json.insert(0, release)

    # Update file
    with open("./website/releases.json", "w+") as f:
        json.dump(releases_json, f, indent=4)

if __name__ == "__main__":
    main(*argv[1:])
