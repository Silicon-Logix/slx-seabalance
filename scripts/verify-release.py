#!/usr/bin/env python3
# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

"""Verify the exact contents and checksums of a SeaBalance export or distribution."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

EXTRAS = {"manifest.json", "SHA256SUMS.txt", "SHA256SUMS.txt.sig"}
BANNED = {"private", "build", "workspace", "validation", ".git", ".vs", "__pycache__"}
GITHUB_EXCLUDED_DIRS = {"core", "dist", "critical-replay", "mp1-replay", "replay-demo"}
GITHUB_ASSETS = {".glb", ".ico", ".png"}
GITHUB_TEXT = {".c", ".cpp", ".h", ".hpp", ".cmake", ".md", ".txt", ".ps1", ".py", ".json",
               ".conf", ".inc", ".bb", ".service", ".env", ".cmd", ".csv", ".fragment", ".in",
               ".mit"}
GITHUB_BASENAMES = {".gitattributes", ".gitignore", "LICENSE", "20-slx-seabalance-panel"}
GITHUB_VESSEL = "assets/models/SLX_SeaBalance_Vessel_Runtime.glb"


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def valid(name):
    path = PurePosixPath(name)
    return (bool(path.parts) and not path.is_absolute() and ".." not in path.parts
            and not (set(part.lower() for part in path.parts) & BANNED)
            and "\\" not in name and ":" not in name and "\n" not in name)


def github_path(name):
    path = PurePosixPath(name)
    return (
        not (set(path.parts) & GITHUB_EXCLUDED_DIRS)
        and (path.name in GITHUB_BASENAMES or path.suffix.lower() in GITHUB_TEXT
             or (path.parts[0] == "assets" and path.suffix in GITHUB_ASSETS))
    )


def entries(root, github_source):
    for item in root.iterdir():
        if github_source and item.name == ".git":
            continue
        yield item
        if item.is_dir() and not item.is_symlink():
            yield from item.rglob("*")


def verify(root):
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    if manifest["schema"] != 1 or manifest["app"] != "SLX SeaBalance":
        raise ValueError("Unknown release manifest")
    locked = {entry["path"]: entry for entry in manifest["files"]}
    if len(locked) != len(manifest["files"]) or any(not valid(name) for name in locked):
        raise ValueError("Invalid or duplicate manifest path")
    github_source = manifest["package"] == "github-source"
    if github_source and any(not github_path(name) for name in locked):
        raise ValueError("GitHub source tree contains an excluded file")
    if github_source and {name for name in locked if name.lower().endswith(".glb")} != {GITHUB_VESSEL}:
        raise ValueError("GitHub source tree must contain only the runtime vessel model")
    actual = set()
    for path in entries(root, github_source):
        if path.is_symlink():
            raise ValueError(f"Unexpected symlink {path.name}")
        if path.is_file():
            actual.add(path.relative_to(root).as_posix())
    if actual - EXTRAS != set(locked):
        raise ValueError(f"Contents differ from manifest: {sorted((actual - EXTRAS) ^ set(locked))}")
    for name, entry in locked.items():
        path = root / name
        if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"]:
            raise ValueError(f"Incorrect file size or digest: {name}")
    lines = (root / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
    checksums = dict(line.split("  ", 1)[::-1] for line in lines if line and not line.startswith("#"))
    expected = {name: item["sha256"] for name, item in locked.items()}
    expected["manifest.json"] = digest(root / "manifest.json")
    if checksums != expected:
        raise ValueError("Checksum index does not match the manifest")
    if manifest["signed_checksums"] != ("SHA256SUMS.txt.sig" in actual):
        raise ValueError("Detached signature does not match the manifest")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.root)
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"SeaBalance release: {error}", file=sys.stderr)
        return 1
    print(f'{result["app"]} {result["version"]} {result["package"]}: {len(result["files"])} files verified')
    return 0


if __name__ == "__main__":
    sys.exit(main())
