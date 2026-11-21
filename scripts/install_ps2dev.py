"""Install the checksum-pinned official PS2DEV SDK without root.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import json
import platform
from pathlib import Path
import sys
from common import ROOT, download, extract, linux_only


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--directory", type=Path, default=Path.home() / ".local/eelinux-ps2dev")
    p.add_argument("--cache", type=Path, default=Path.home() / ".cache/eelinux/downloads")
    p.add_argument("--lock", type=Path, default=ROOT / "toolchain/ps2dev-binary-lock.json",
                   help="SDK binary lock file (default: toolchain/ps2dev-binary-lock.json)")
    a = p.parse_args()
    linux_only()
    if platform.machine() not in ("x86_64", "amd64"):
        raise RuntimeError("Pinned SDK is x86_64; select and verify an appropriate upstream asset for this host")
    lock = json.loads(a.lock.read_text())
    marker = a.directory / "eelinux-sdk-source.json"
    if " " in str(a.directory.resolve()):
        raise RuntimeError("SDK directory must have no spaces")
    if a.directory.exists():
        if not marker.is_file() or json.loads(marker.read_text()) != lock:
            raise RuntimeError("Existing SDK directory does not match the pinned installation; use a new directory")
    else:
        archive = download(lock["asset_url"], a.cache / f"ps2dev-ubuntu-{lock['sha256'][:16]}.tar.gz", lock["sha256"])
        extract(archive, a.directory)
        marker.write_text(json.dumps(lock, indent=2) + "\n")
    matches = list(a.directory.glob("ps2dev/iop/bin/mipsel-none-elf-gcc"))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one actual IOP GCC binary; found {len(matches)}")
    print(f"PS2DEV IOP prefix: {str(matches[0])[:-3]}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        print(f"ps2dev: {exc}", file=sys.stderr)
        sys.exit(1)
