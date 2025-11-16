"""

This file formats the README into a plaintext-compatible version for releases.

"""

from sys import argv
import re

def remove_until_section(body: str, header: str) -> str:
    # Find start of section
    start = re.search(rf"^## {header}", body, flags=re.MULTILINE)
    if start is None: return body
    start = start.start()
    return body[start:]

def remove_section(body: str, header: str) -> str:
    # Find start of section
    start = re.search(rf"^## {header}", body, flags=re.MULTILINE)
    if start is None: return body
    start = start.start()

    # Find end of section
    end = re.search(r"^## ", body[start+1:], flags=re.MULTILINE)

    if end is not None:
        end = end.start() + start + 1

    return body[:start] + (body[end:] if end is not None else "")

def markout_sections(body: str) -> str:
    # Find start of each section
    separator = (("=" * 100) + "\n") * 2
    offset = 0
    while True:
        start = re.search(rf"^## ", body[offset:], flags=re.MULTILINE)
        if start is None: return body

        start = start.start() + offset
        offset = start + 1 + len(separator) * 2

        # Add line marking before
        body = body[:start] \
            + separator \
            + body[start:body.find("\n", start)] \
            + "\n" + separator \
            + body[body.find("\n", start)+1:]

def replace_hyperlinks(body: str) -> str:
    # Image hyperlinks first
    body = re.sub(r"\[\!\[.*?\]\(.*?\)\]\((.*?)\)", r"\1", body)

    # Hyperlinks
    body = re.sub(r"\[(.*?)\]\((.*?)\)", r"\1 (\2)", body)

    return body

def main(path: str):
    body = ""

    # Read
    with open(path, "r") as f:
        body = f.read()

    # Replace first section
    body = remove_until_section(body, "Table of Contents")

    # Remove sections
    body = remove_section(body, "Table of Contents")
    body = remove_section(body, "About")
    body = remove_section(body, "For Developers")
    body = remove_section(body, "Contributing")
    body = remove_section(body, "Changelog")
    body = remove_section(body, "Credits")
    body = remove_section(body, "Fun Facts")

    # Mark out each section for visual clarity
    body = markout_sections(body)

    # Add title & heading
    body = "# Mercury\n" \
        + "## A lightweight, configurable HTTP server\n" \
        + "### Project by Travis Heavener\n\n" \
        + body

    # Replace hyperlinks
    body = replace_hyperlinks(body)

    # Misc. escapes
    body = body.replace("\\*", "*")

    # Write
    with open(path, "w") as f:
        f.write(body)

if __name__ == "__main__":
    if len(argv) < 2:
        print("Invalid usage: py <script> /path/to/README")
        exit(1)

    main(argv[1])