#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Package a verified cgit runtime and bare source repository as one zipapp."""

from hashlib import sha256
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parents[1]
RUNTIME_FILES = {
    "cgit", "cgit.css", "cgit.js", "cgit.png", "favicon.ico",
    "eelinux-build.json", "install.py", "source-lock.json", "runtime-bundle.json",
}


def git(repository, *args):
    return subprocess.check_output(
        ["git", "--git-dir", str(repository), *args], text=True
    ).strip()


def verified_runtime(directory):
    directory = Path(directory).resolve()
    if {p.name for p in directory.iterdir()} != RUNTIME_FILES:
        raise ValueError("Runtime must contain exactly the staged runtime inventory")
    lock = json.loads((HERE / "source-lock.json").read_text(encoding="utf-8"))
    bundle = json.loads((directory / "runtime-bundle.json").read_text(encoding="utf-8"))
    if bundle.get("schema") != 1 or bundle.get("cgit_commit") != lock["commit"] or bundle.get("git_commit") != lock["git_submodule_commit"]:
        raise ValueError("Runtime revision does not match the source lock")
    if set(bundle["files"]) != RUNTIME_FILES - {"runtime-bundle.json"}:
        raise ValueError("Runtime has an unexpected hash inventory")
    files = {}
    for name in sorted(RUNTIME_FILES):
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError("Runtime contains a link or non-file: " + name)
        data = path.read_bytes()
        if name != "runtime-bundle.json" and sha256(data).hexdigest() != bundle["files"][name]:
            raise ValueError("Runtime hash mismatch: " + name)
        files["runtime/" + name] = data
    report = json.loads(files["runtime/eelinux-build.json"])
    binary = files["runtime/cgit"]
    if binary[:6] != b"\x7fELF\x02\x01" or int.from_bytes(binary[18:20], "little") != 62:
        raise ValueError("The runtime must be a little-endian Linux x86_64 ELF")
    if report.get("build_mode") != "static" or report.get("elf_has_dynamic_interpreter") is not False:
        raise ValueError("The standalone installer requires the verified static runtime")
    if sha256(binary).hexdigest() != report["binary_sha256"]:
        raise ValueError("Runtime executable differs from its build report")
    return files, lock


def create(runtime, repository, output):
    repository = Path(repository).resolve()
    output = Path(output).resolve()
    if output.suffix != ".pyz" or output.is_relative_to(SOURCE_ROOT):
        raise ValueError("Write the .pyz outside the project checkout")
    if output.exists():
        raise ValueError("Installer output already exists; use a new filename")
    files, lock = verified_runtime(runtime)
    if git(repository, "rev-parse", "--is-bare-repository") != "true":
        raise ValueError("Supply a bare source repository")
    if git(repository, "symbolic-ref", "HEAD") != "refs/heads/main":
        raise ValueError("The repository default branch must be main")
    commit = git(repository, "rev-parse", "refs/heads/main")
    if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
        raise ValueError("Unexpected source commit identity")
    refs = dict(
        line.split(" ", 1) for line in
        git(repository, "for-each-ref", "--format=%(refname) %(objectname)").splitlines()
    )
    if not refs or any(not ref.startswith(("refs/heads/", "refs/tags/")) for ref in refs):
        raise ValueError("Source repository must contain only public branches and tags")
    git(repository, "fsck", "--full", "--no-reflogs")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eelinux-installer-", dir=output.parent) as temporary:
        bundle = Path(temporary) / "repository.bundle"
        subprocess.run(
            ["git", "--git-dir", str(repository), "bundle", "create", str(bundle), "--all"],
            check=True,
        )
        files["repository.bundle"] = bundle.read_bytes()
        files["__main__.py"] = (HERE / "installer_main.py").read_bytes()
        inventory = {
            name: {
                "sha256": sha256(data).hexdigest(), "bytes": len(data),
                "mode": 0o700 if name == "runtime/cgit" else 0o600,
            }
            for name, data in files.items()
        }
        manifest = {
            "schema": 1, "source_commit": commit, "source_branch": "main",
            "source_refs": refs, "target_platform": "linux-x86_64",
            "cgit_commit": lock["commit"], "git_commit": lock["git_submodule_commit"],
            "files": inventory,
        }
        temporary_archive = Path(temporary) / "installer.pyz"
        with zipfile.ZipFile(temporary_archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | inventory[name]["mode"]) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)
            archive.writestr("installer-manifest.json", json.dumps(manifest, indent=2) + "\n")
        if output.exists():
            raise ValueError("Installer output appeared during packaging; left untouched")
        os.replace(temporary_archive, output)
    print("Installer:", output)
    print("Source commit:", commit)
    print("SHA-256:", sha256(output.read_bytes()).hexdigest())
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        create(args.runtime, args.repository, args.output)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, "Installer packaging stopped: " + str(error) + "\n")
