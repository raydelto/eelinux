#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Copy only the verified cgit runtime into a fresh external upload directory."""

import argparse
from hashlib import sha256
import json
import platform
from pathlib import Path
import shutil
import subprocess

import install


def stage(source, destination, strip=False):
    source, report = install.load_build(source)
    report = dict(report)
    destination = install.account_path(destination)
    if destination.is_relative_to(Path(__file__).resolve().parents[2]):
        raise ValueError("Runtime bundles must be staged outside the source checkout")
    if destination.is_symlink() or destination.exists() and any(destination.iterdir()):
        raise ValueError("Use a fresh empty runtime-bundle directory")
    destination.mkdir(parents=True, exist_ok=True)
    files = {}
    for name in ("cgit", "eelinux-build.json", *install.ASSETS):
        shutil.copyfile(source / name, destination / name)
    if strip:
        subprocess.run(["strip", "--strip-unneeded", str(destination / "cgit")], check=True)
        report["source_binary_sha256"] = report["binary_sha256"]
        report["binary_sha256"] = sha256((destination / "cgit").read_bytes()).hexdigest()
        report["runtime_stripped"] = True
        (destination / "eelinux-build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for name in ("cgit", "eelinux-build.json", *install.ASSETS):
        files[name] = sha256((destination / name).read_bytes()).hexdigest()
    for name in ("install.py", "source-lock.json"):
        shutil.copyfile(Path(__file__).with_name(name), destination / name)
        files[name] = sha256((destination / name).read_bytes()).hexdigest()
    manifest = {"schema": 1, "cgit_commit": report["cgit_commit"], "git_commit": report["git_commit"],
                "build_machine": platform.machine(), "build_mode": report["build_mode"], "files": files,
                "scope": "Generated from the verified pinned build. Hashes identify bundled bytes; they are not an independent publisher signature."}
    (destination / "runtime-bundle.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Staged verified runtime:", destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--destination", required=True)
    parser.add_argument("--strip", action="store_true", help="Strip only the staged runtime and record its resulting hash")
    args = parser.parse_args()
    stage(args.build_dir, args.destination, args.strip)
