#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create an external, self-contained theme updater for the hosting account."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import stat
import zipfile

HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parents[2]
ASSETS = ("eelinux-cgit.css", "eelinux-logo.svg", "favicon.svg",
          "head.html", "header.html", "footer.html")
BOOTSTRAP = '''"""Validate the uploaded theme package before running its installer."""
from hashlib import sha256
import json
from pathlib import Path
import platform
import stat
import subprocess
import sys
import tempfile
import zipfile

sys.dont_write_bytecode = True
NAMES = {"__main__.py", "install_theme.py", "eelinux-cgit.css", "eelinux-logo.svg",
         "favicon.svg", "head.html", "header.html", "footer.html"}
try:
    if sys.version_info < (3, 9) or platform.system() != "Linux":
        raise ValueError("Run this updater with Python 3.9+ in the Linux hosting account")
    if any(arg == "--theme-dir" or arg.startswith("--theme-dir=") for arg in sys.argv[1:]):
        raise ValueError("The uploaded package supplies its own verified theme directory")
    with zipfile.ZipFile(Path(sys.argv[0]).resolve()) as archive:
        entries = archive.infolist()
        if len(entries) != len(NAMES) + 1 or {entry.filename for entry in entries} != NAMES | {"theme-package.json"}:
            raise ValueError("Unexpected or duplicate package entry")
        if sum(entry.file_size for entry in entries) > 5 * 1024 * 1024:
            raise ValueError("Theme package exceeds its supported size")
        for entry in entries:
            mode = entry.external_attr >> 16
            if entry.is_dir() or entry.flag_bits & 1 or stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                raise ValueError("Theme package contains a link, directory or unsupported file")
        manifest = json.loads(archive.read("theme-package.json"))
        if manifest.get("schema") != 1 or set(manifest.get("files", {})) != NAMES:
            raise ValueError("Unexpected theme package manifest")
        payload = {}
        for name, record in manifest["files"].items():
            data = archive.read(name)
            if len(data) != record["bytes"] or sha256(data).hexdigest() != record["sha256"]:
                raise ValueError("Theme package hash mismatch: " + name)
            payload[name] = data
    with tempfile.TemporaryDirectory(prefix="eelinux-cgit-theme-") as temporary:
        folder = Path(temporary)
        folder.chmod(0o700)
        for name, data in payload.items():
            target = folder / name
            target.write_bytes(data)
            target.chmod(0o600)
        result = subprocess.run([sys.executable, "-B", str(folder / "install_theme.py"),
                                 "--theme-dir", str(folder), *sys.argv[1:]])
        raise SystemExit(result.returncode)
except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
    raise SystemExit("Theme update stopped: " + str(error))
'''


def create(output_directory):
    directory = Path(output_directory).resolve()
    if directory.is_relative_to(SOURCE_ROOT):
        raise ValueError("Keep generated upload packages outside the source project")
    files = {}
    identity = sha256()
    for name in (*ASSETS, "install_theme.py"):
        path = HERE / name
        if path.is_symlink() or not path.is_file():
            raise ValueError("Missing or linked theme input: " + name)
        data = path.read_bytes()
        files[name] = data
    for name in sorted(ASSETS):
        data = files[name]
        identity.update(name.encode("ascii") + b"\0" + len(data).to_bytes(8, "big") + data)
    files["__main__.py"] = BOOTSTRAP.encode("utf-8")
    manifest = {"schema": 1, "theme_id": identity.hexdigest()[:16],
                "scope": "Presentation update only; source repository and cgit binary are retained.",
                "files": {name: {"bytes": len(data), "sha256": sha256(data).hexdigest()}
                          for name, data in sorted(files.items())}}
    manifest_data = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    package_id = sha256(manifest_data).hexdigest()[:16]
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / ("eelinux-cgit-theme-" + package_id + ".pyz")
    if destination.exists():
        raise ValueError("Upload package already exists; existing file left untouched")
    with zipfile.ZipFile(destination, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted({**files, "theme-package.json": manifest_data}.items()):
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o600) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    report = {"package": str(destination), "theme_id": manifest["theme_id"],
              "bytes": destination.stat().st_size,
              "sha256": sha256(destination.read_bytes()).hexdigest()}
    (directory / "latest-theme-package.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-directory", required=True)
    args = parser.parse_args()
    print(json.dumps(create(args.output_directory), indent=2))
