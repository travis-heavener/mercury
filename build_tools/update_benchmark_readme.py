#!/usr/bin/env python3
"""
Updates Performance tables in README.md from benchmark markdown outputs
"""

import argparse
import os
import re
import sys
from typing import Optional, Tuple


def extract_table_and_footnote(content: str) -> Tuple[str, Optional[str]]:
    # Strip leading and trailing whitespace
    text = content.strip()
    footnote = None

    # Search for trailing baseline footnote
    footnote_match = re.search(r"(\*[Bb]aseline averages[^\n]+\*)\s*$", text)
    if footnote_match:
        footnote = footnote_match.group(1).strip()
        table_text = text[:footnote_match.start()].strip()
    else:
        table_text = text

    return table_text, footnote


def resolve_md_file(path: Optional[str]) -> Optional[str]:
    # Resolve path to markdown file from directory or file path
    if not path or not os.path.exists(path):
        return None
    if os.path.isfile(path):
        return path
    if os.path.isdir(path):
        for entry in sorted(os.listdir(path)):
            if entry.endswith(".md"):
                return os.path.join(path, entry)
    return None


def update_readme(
    readme_path: str,
    linux_md_path: Optional[str] = None,
    windows_md_path: Optional[str] = None,
    dry_run: bool = False,
) -> bool:
    if not os.path.exists(readme_path):
        print(f"Error: README file not found: {readme_path}", file=sys.stderr)
        return False

    with open(readme_path, "r", encoding="utf-8") as f:
        readme_content = f.read()

    # Locate Performance section boundaries
    perf_header = "### Performance"
    if perf_header not in readme_content:
        print(f"Error: Could not locate '{perf_header}' in {readme_path}", file=sys.stderr)
        return False

    # Find separator following Performance section
    end_marker = "\n---\n"
    start_idx = readme_content.find(perf_header)
    end_idx = readme_content.find(end_marker, start_idx)

    if end_idx == -1:
        print(f"Error: Could not locate section end delimiter '{end_marker}' after {perf_header}", file=sys.stderr)
        return False

    perf_section = readme_content[start_idx:end_idx]

    # Parse new content
    linux_table = None
    windows_table = None
    footnote = None

    linux_file = resolve_md_file(linux_md_path)
    windows_file = resolve_md_file(windows_md_path)
    if (linux_md_path and not linux_file) or (windows_md_path and not windows_file):
        print("Error: Requested benchmark Markdown input is missing", file=sys.stderr)
        return False

    if linux_file:
        with open(linux_file, "r", encoding="utf-8") as f:
            tbl, fn = extract_table_and_footnote(f.read())
            linux_table = tbl
            if fn:
                footnote = fn

    if windows_file:
        with open(windows_file, "r", encoding="utf-8") as f:
            tbl, fn = extract_table_and_footnote(f.read())
            windows_table = tbl
            if fn:
                footnote = fn

    if not linux_table and not windows_table:
        print("Error: No valid benchmark inputs provided via --linux or --windows", file=sys.stderr)
        return False

    # Default footnote fallback if none found in outputs
    if not footnote:
        footnote = "*Baseline averages across 5 separate trials over loopback using tests/conf_files/benchmark.*"

    # Extract existing Linux and Windows blocks if updating only one platform
    existing_linux_match = re.search(r"(#### Linux[^\n]*\n\n\|.*?)(?=\n\n####|\n\n\*[Bb]aseline|\Z)", perf_section, re.DOTALL)
    existing_windows_match = re.search(r"(#### Windows[^\n]*\n\n\|.*?)(?=\n\n####|\n\n\*[Bb]aseline|\Z)", perf_section, re.DOTALL)

    final_linux = (linux_table or (existing_linux_match.group(1) if existing_linux_match else "")).strip()
    final_windows = (windows_table or (existing_windows_match.group(1) if existing_windows_match else "")).strip()

    # Reassemble performance block
    section_intro = "### Performance\n\nThe following measurements compare Mercury, Apache, and Nginx under the benchmark conditions listed below:"
    blocks = [section_intro]
    if final_linux:
        blocks.append(final_linux)
    if final_windows:
        blocks.append(final_windows)
    blocks.append(footnote)

    new_perf_section = "\n\n".join(blocks)

    updated_readme = readme_content[:start_idx] + new_perf_section + readme_content[end_idx:]

    if dry_run:
        print("=== DRY RUN: Updated Performance Section ===")
        print(new_perf_section)
        print("===========================================")
        return True

    with open(readme_path, "w", encoding="utf-8") as f:
        f.write(updated_readme)

    print(f"Successfully updated Performance section in {readme_path}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Update Performance benchmark tables in README.md")
    parser.add_argument("--readme", default="README.md", help="Path to README.md file")
    parser.add_argument("--linux", help="Path to Linux benchmark markdown output")
    parser.add_argument("--windows", help="Path to Windows benchmark markdown output")
    parser.add_argument("--dry-run", action="store_true", help="Print updated section without writing to file")

    args = parser.parse_args()

    success = update_readme(
        readme_path=args.readme,
        linux_md_path=args.linux,
        windows_md_path=args.windows,
        dry_run=args.dry_run,
    )

    if not success:
        sys.exit(1)


if __name__ == "__main__":
    main()
