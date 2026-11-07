#!/usr/bin/env python3
"""Cross-build and install the pinned dhcpcd client without rebuilding GNU tools.

SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from common import ROOT, download, extract, linux_only
from userland import CPU_FLAGS, toolchain_probe, validate_elf

LOCK = ROOT / "userland/dhcpcd-sources.json"
MANIFEST = ".eelinux-dhcpcd.json"
FILES = {"sbin/dhcpcd": "src/dhcpcd", "usr/share/man/man8/dhcpcd.8": "src/dhcpcd.8",
         "usr/share/man/man5/dhcpcd.conf.5": "src/dhcpcd.conf.5",
         "usr/share/licenses/dhcpcd/LICENSE": "LICENSE"}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(folder: Path) -> dict:
    files = {}
    for path in sorted(folder.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Unexpected dhcpcd source/payload symbolic link: {path}")
        if path.is_file():
            files[path.relative_to(folder).as_posix()] = {
                "sha256": sha(path), "bytes": path.stat().st_size}
    return files


def execute(argv, *, cwd=None, log=None, env=None) -> bytes:
    result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log is not None:
        log.write(("+ " + " ".join(map(str, argv)) + "\n").encode())
        log.write(result.stdout); log.flush()
    if result.returncode:
        raise RuntimeError(f"dhcpcd command failed ({result.returncode}): {argv}\n"
                           + result.stdout.decode(errors="replace")[-3000:])
    return result.stdout


def build(args) -> dict:
    linux_only()
    if args.jobs < 1:
        raise RuntimeError("--jobs must be positive")
    work = args.work.resolve(); payload = args.staging.resolve()
    if payload.exists() or (work / "target").exists():
        raise RuntimeError("Preserve previous outputs; select fresh work/payload directories")
    if work == payload or work.is_relative_to(payload) or payload.is_relative_to(work):
        raise RuntimeError("Build work and payload must be separate directories")
    work.mkdir(parents=True, exist_ok=True)
    lock = json.loads(LOCK.read_text())
    archive = work / "downloads" / lock["archive"]["name"]
    download(lock["archive"]["url"], archive, lock["archive"]["sha256"])
    if archive.stat().st_size != lock["archive"]["bytes"]:
        raise RuntimeError("dhcpcd archive size differs from publisher's release")
    source = work / "sources" / lock["archive"]["root"]
    record = work / "source-inventory.json"
    if source.exists():
        if not record.exists() or inventory(source) != json.loads(record.read_text()):
            raise RuntimeError("dhcpcd source changed or has no inventory; use a fresh work directory")
    else:
        extract(archive, work / "sources")
        record.write_text(json.dumps(inventory(source), indent=2) + "\n")
    if sha(source / "LICENSE") != lock["license_sha256"]:
        raise RuntimeError("dhcpcd license changed")
    compiler = toolchain_probe(args.toolchain_prefix, work / "toolchain", "base")
    target = work / "target"
    shutil.copytree(source, target)
    for item in lock.get("patches", []):
        patch = ROOT / item["path"]
        if sha(patch) != item["sha256"]:
            raise RuntimeError("dhcpcd patch checksum mismatch")
        execute(["git", "apply", "--check", str(patch)], cwd=target)
        execute(["git", "apply", str(patch)], cwd=target)
    environment = os.environ.copy()
    environment.update(CC=compiler["compiler"], CFLAGS=" ".join([*CPU_FLAGS, "-Os"]),
                       CPPFLAGS="-D_FILE_OFFSET_BITS=64", LDFLAGS=" ".join(CPU_FLAGS))
    with (work / "build.log").open("wb") as log:
        execute(["./configure", "--host=" + compiler["tuple"], *lock["configuration"]],
                cwd=target, env=environment, log=log)
        execute(["make", "-j", str(args.jobs)], cwd=target, env=environment, log=log)
        execute([args.toolchain_prefix + "strip", "--strip-unneeded", target / "src/dhcpcd"], log=log)
    validate_elf(target / "src/dhcpcd")
    for name, original in FILES.items():
        destination = payload / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(target / original, destination)
        destination.chmod(0o755 if name == "sbin/dhcpcd" else 0o644)
    files = inventory(payload)
    report = {"schema": 1, "package": "dhcpcd", "version": lock["version"],
              "source_lock_sha256": sha(LOCK), "recipe_sha256": sha(Path(__file__)),
              "source_inventory_sha256": sha(record), "source_archive_sha256": sha(archive),
              "patches": lock.get("patches", []),
              "configuration": lock["configuration"], "CPU_FLAGS": CPU_FLAGS,
              "CPPFLAGS": "-D_FILE_OFFSET_BITS=64",
              "compiler": {key: compiler[key] for key in ("tuple", "version", "headers")},
              "files": files, "total_regular_file_bytes": sum(x["bytes"] for x in files.values()),
              "privilege_separation": True, "seccomp": False, "DHCP_packet_tested": False}
    (payload / MANIFEST).write_text(json.dumps(report, indent=2) + "\n")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Built dhcpcd {lock['version']}: {payload} ({report['total_regular_file_bytes']} bytes)")
    return report


def install(payload: Path, staging: Path) -> dict:
    linux_only()
    payload = payload.resolve(strict=True); staging = staging.resolve(strict=True)
    if payload == staging or payload.is_relative_to(staging) or staging.is_relative_to(payload):
        raise RuntimeError("dhcpcd payload and GNU staging must be separate")
    if not (staging / "usr").is_dir():
        raise RuntimeError("An existing GNU rootfs stage is required")
    report = json.loads((payload / MANIFEST).read_text())
    files = inventory(payload); files.pop(MANIFEST)
    if (report.get("source_lock_sha256") != sha(LOCK) or report.get("recipe_sha256") != sha(Path(__file__))
            or report.get("files") != files or set(files) != set(FILES)):
        raise RuntimeError("dhcpcd payload provenance/inventory mismatch")
    if files["usr/share/licenses/dhcpcd/LICENSE"]["sha256"] != json.loads(LOCK.read_text())["license_sha256"]:
        raise RuntimeError("dhcpcd license differs from pinned upstream")
    validate_elf(payload / "sbin/dhcpcd")
    metadata = "usr/share/eelinux/dhcpcd.json"
    for name in [*files, metadata]:
        destination = staging / name
        if not destination.resolve().is_relative_to(staging):
            raise RuntimeError("dhcpcd destination escapes staging")
        for parent in destination.parents:
            if parent == staging: break
            if parent.is_symlink() or parent.exists() and not parent.is_dir():
                raise RuntimeError("Invalid dhcpcd destination parent")
        if destination.is_symlink() or destination.exists() and not destination.is_file():
            raise RuntimeError("dhcpcd destination is not a regular file")
        if name in files and destination.exists() and sha(destination) != files[name]["sha256"]:
            raise RuntimeError("Preserve previous dhcpcd output; use a fresh GNU stage")
    for name in files:
        destination = staging / name; destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(payload / name, destination)
        destination.chmod(0o755 if name == "sbin/dhcpcd" else 0o644)
    destination = staging / metadata; destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n")
    return {**report, "installed_files": sorted([*files, metadata])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("build")
    p.add_argument("--work", type=Path, required=True)
    p.add_argument("--staging", type=Path, required=True)
    p.add_argument("--toolchain-prefix", required=True)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--report", type=Path)
    p = commands.add_parser("install")
    p.add_argument("--payload", type=Path, required=True)
    p.add_argument("--staging", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "build": build(args)
    else: install(args.payload, args.staging)


if __name__ == "__main__":
    main()
