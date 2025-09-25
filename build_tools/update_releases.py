import json
from sys import argv
import time

def main(version_full: str, linux_hash: str, win_hash: str):
    version = version_full.replace("Mercury ", "")

    # Load existing JSON
    releases_json: list = None

    with open("./docs/releases.json", "r") as f:
        releases_json = json.load(f)

    # Remove existing releases with matching version
    releases_json = [ r for r in releases_json if r["name"] != version ]

    # Prepend new version entry
    releases_json.insert(0, {
        "name": version,
        "date": int(round(time.time() * 1000)),
        "linuxHash": linux_hash,
        "winHash": win_hash
    })

    # Update file
    with open("./docs/releases.json", "w+") as f:
        json.dump(releases_json, f, indent=4)

if __name__ == "__main__":
    main(*argv[1:4])