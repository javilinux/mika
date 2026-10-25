#!/usr/bin/env python3
"""CLI helper to browse and download M5Burner firmwares for StackChan & CoreS3."""

import argparse
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

API_URL = "https://m5burner-api.m5stack.com/api/firmware?v=3.0.0"
CDN_BASE = "https://m5burner-cdn.m5stack.com/firmware/"
DEFAULT_OUT_DIR = Path(__file__).resolve().parent.parent / "backups" / "firmwares"


def fetch_catalog():
    req = urllib.request.Request(API_URL, headers={"User-Agent": "M5Burner-CLI/1.0"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def cmd_list(args):
    catalog = fetch_catalog()
    query = (args.filter or "").lower()
    print(f"{'CATEGORY':<12} | {'LATEST':<10} | {'NAME':<42} | {'AUTHOR'}")
    print("-" * 85)
    for item in catalog:
        cat = str(item.get("category", ""))
        name = str(item.get("name", ""))
        author = str(item.get("author", ""))
        if cat not in ("stackchan", "cores3") and "stackchan" not in name.lower():
            continue
        if query and query not in name.lower() and query not in cat.lower():
            continue
        versions = item.get("versions", [])
        latest = versions[-1].get("version", "?") if versions else "?"
        print(f"{cat:<12} | {latest:<10} | {name:<42} | {author}")


def cmd_download(args):
    catalog = fetch_catalog()
    target = args.name.lower()
    matched = None
    for item in catalog:
        if item.get("name", "").lower() == target or target in item.get("name", "").lower():
            matched = item
            break
    if not matched:
        print(f"Error: Firmware matching '{args.name}' not found.", file=sys.stderr)
        sys.exit(1)

    versions = matched.get("versions", [])
    if not versions:
        print(f"Error: No versions listed for {matched.get('name')}.", file=sys.stderr)
        sys.exit(1)

    ver_entry = versions[-1]
    ver_str = ver_entry.get("version", "latest")
    file_hash = ver_entry.get("file")
    url = CDN_BASE + file_hash

    DEFAULT_OUT_DIR.mkdir(parents=True, exist_ok=True)
    safe_name = matched["name"].replace(" ", "_").replace("/", "_")
    out_path = DEFAULT_OUT_DIR / f"{safe_name}_{ver_str}.bin"

    print(f"Downloading {matched['name']} ({ver_str}) from {url} ...")
    urllib.request.urlretrieve(url, out_path)
    size_mb = out_path.stat().st_size / (1024 * 1024)
    print(f"OK: Saved to {out_path} ({size_mb:.2f} MB)")


def main():
    parser = argparse.ArgumentParser(description="M5Burner CLI helper for StackChan / CoreS3")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_list = sub.add_parser("list", help="List StackChan and CoreS3 firmwares")
    p_list.add_argument("filter", nargs="?", default="", help="Optional text filter")
    p_list.set_defaults(func=cmd_list)

    p_dl = sub.add_parser("download", help="Download latest .bin for a firmware by name")
    p_dl.add_argument("name", help="Firmware name or substring (e.g. StackChan-UserDemo)")
    p_dl.set_defaults(func=cmd_download)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
