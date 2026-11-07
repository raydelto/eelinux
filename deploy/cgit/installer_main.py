#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run from the hosting account's existing shell; this is not a web endpoint."""

from hashlib import sha256
import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import stat
import subprocess
import sys
import tempfile
import zipfile

sys.dont_write_bytecode = True
RUNTIME_NAMES = {
    "cgit", "cgit.css", "cgit.js", "cgit.png", "favicon.ico",
    "eelinux-build.json", "install.py", "source-lock.json", "runtime-bundle.json",
}
EXPECTED_FILES = {"__main__.py", "repository.bundle"} | {
    "runtime/" + name for name in RUNTIME_NAMES
}


def checked_archive(path):
    """Validate all names and hashes before extracting or importing any payload."""
    archive = zipfile.ZipFile(path)
    try:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(names) != len(set(names)) or set(names) != EXPECTED_FILES | {"installer-manifest.json"}:
            raise ValueError("Unexpected or duplicate installer archive entry")
        if sum(entry.file_size for entry in entries) > 200 * 1024 * 1024:
            raise ValueError("Installer exceeds its supported unpacked size")
        for entry in entries:
            name = PurePosixPath(entry.filename)
            mode = entry.external_attr >> 16
            if name.is_absolute() or ".." in name.parts or "\\" in entry.filename or entry.is_dir():
                raise ValueError("Unsafe archive pathname")
            if stat.S_ISLNK(mode) or stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                raise ValueError("Installer contains a link or special file")
            if entry.flag_bits & 1:
                raise ValueError("Encrypted installer entries are unsupported")
        manifest = json.loads(archive.read("installer-manifest.json"))
        if manifest.get("schema") != 1 or manifest.get("target_platform") != "linux-x86_64":
            raise ValueError("Unsupported installer manifest")
        if set(manifest["files"]) != EXPECTED_FILES:
            raise ValueError("Manifest and archive inventories differ")
        if not re.fullmatch("[0-9a-f]{40}", manifest["source_commit"]):
            raise ValueError("Invalid source commit identity")
        if manifest["source_branch"] != "main":
            raise ValueError("Unexpected default branch")
        for name, record in manifest["files"].items():
            entry = archive.getinfo(name)
            if entry.file_size != record["bytes"]:
                raise ValueError("Installer length mismatch: " + name)
            digest = sha256()
            with archive.open(name) as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest() != record["sha256"]:
                raise ValueError("Installer hash mismatch: " + name)
            wanted_mode = 0o700 if name == "runtime/cgit" else 0o600
            if record["mode"] != wanted_mode:
                raise ValueError("Unexpected installer file permissions")
        return archive, manifest
    except BaseException:
        archive.close()
        raise


def git(repository, *args):
    return subprocess.check_output(
        ["git", "--git-dir", str(repository), *args], text=True
    ).strip()


def verify_repository(repository, manifest):
    if git(repository, "rev-parse", "--is-bare-repository") != "true":
        raise ValueError("Existing repository is not bare; left untouched")
    if git(repository, "symbolic-ref", "HEAD") != "refs/heads/main":
        raise ValueError("Repository default branch differs; left untouched")
    if git(repository, "rev-parse", "refs/heads/main") != manifest["source_commit"]:
        raise ValueError("Repository source commit differs; left untouched")
    refs = dict(
        line.split(" ", 1) for line in
        git(repository, "for-each-ref", "--format=%(refname) %(objectname)").splitlines()
    )
    if refs != manifest["source_refs"]:
        raise ValueError("Repository refs differ; left untouched")
    git(repository, "fsck", "--full", "--no-reflogs")


def sanitized_git_environment():
    for name in list(os.environ):
        if name.startswith("GIT_"):
            del os.environ[name]
    os.environ.update({
        "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": "/dev/null",
        "GIT_CONFIG_SYSTEM": "/dev/null", "GIT_TERMINAL_PROMPT": "0",
        "PYTHONDONTWRITEBYTECODE": "1",
    })


def run_installer(archive_path, args):
    if sys.version_info < (3, 9):
        raise ValueError("Python 3.9 or newer is required")
    if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise ValueError("This runtime requires a Linux x86_64 hosting server")
    sanitized_git_environment()
    archive, manifest = checked_archive(archive_path)
    with archive:
        # Only private temporary extraction occurs before the install preflight.
        work_root = Path(args.work_root)
        if not work_root.is_absolute() or not re.fullmatch(r"/[A-Za-z0-9_./+\-]+", str(work_root)):
            raise ValueError("Use an absolute work path without shell metacharacters")
        for parent in (work_root, *work_root.parents):
            if parent.is_symlink():
                raise ValueError("Work path crosses a symbolic link")
        docroot = Path(args.document_root).resolve()
        work_root = work_root.resolve()
        if work_root == Path("/") or work_root.is_relative_to(docroot) or docroot.is_relative_to(work_root):
            raise ValueError("Setup work directory must be outside the document root")
        work_root.mkdir(mode=0o700, parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="setup-" + manifest["source_commit"][:12] + "-", dir=work_root) as temporary:
            stage = Path(temporary)
            for name, record in manifest["files"].items():
                target = stage / name
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                with archive.open(name) as source, target.open("xb") as destination:
                    for chunk in iter(lambda: source.read(1024 * 1024), b""):
                        destination.write(chunk)
                target.chmod(record["mode"])
            runtime = stage / "runtime"
            spec = importlib.util.spec_from_file_location("eelinux_cgit_installer", runtime / "install.py")
            installer = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = installer
            spec.loader.exec_module(installer)
            installer.LOCK = runtime / "source-lock.json"
            runtime, report = installer.load_build(runtime)
            if report["cgit_commit"] != manifest["cgit_commit"] or report["git_commit"] != manifest["git_commit"]:
                raise ValueError("Installer and runtime revisions differ")
            repository = installer.account_path(args.repository)
            work_root = installer.account_path(work_root)
            if work_root == repository or work_root.is_relative_to(repository) or repository.is_relative_to(work_root):
                raise ValueError("Setup work directory and repository must be separate trees")
            plan, locations = installer.make_plan(
                args.document_root, args.private_root, repository, runtime,
                "https://eelinux.falanet.org/git", "main",
            )
            installer.preflight(plan, locations, update=args.update, existing_public=True)
            existing = repository.exists()
            if existing:
                verify_repository(repository, manifest)
            else:
                repository.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            if args.check_only:
                print("Installer verified; no repository or public files installed.")
                print("Source commit:", manifest["source_commit"])
                return
            if not existing:
                # Clone into a fresh sibling so final publication stays on one filesystem.
                with tempfile.TemporaryDirectory(prefix=".eelinux-repo-", dir=repository.parent) as repo_stage:
                    staged_repo = Path(repo_stage) / "eelinux.git"
                    subprocess.run(
                        ["git", "clone", "--bare", "--no-hardlinks", str(stage / "repository.bundle"), str(staged_repo)],
                        check=True,
                    )
                    verify_repository(staged_repo, manifest)
                    if repository.exists() or repository.is_symlink():
                        raise ValueError("Repository target appeared during setup; left untouched")
                    os.rename(staged_repo, repository)
            verify_repository(repository, manifest)
            installer.install(
                plan, locations, report, update=args.update, existing_public=True
            )
            print("Source commit:", manifest["source_commit"])
            print("Live browsing and HTTP clone still need verification.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--document-root", default="/home/falaneto/public_html/eelinux")
    parser.add_argument("--private-root", default="/home/falaneto/eelinux-cgit")
    parser.add_argument("--repository", default="/home/falaneto/repos/eelinux.git")
    parser.add_argument("--work-root", default="/home/falaneto/eelinux-cgit-setup")
    parser.add_argument("--update", action="store_true", help="Update unchanged hash-recorded cgit files only")
    parser.add_argument("--check-only", action="store_true", help="Verify package, requirements and collision checks without installing")
    args = parser.parse_args()
    try:
        run_installer(Path(sys.argv[0]).resolve(), args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, "cgit setup stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
