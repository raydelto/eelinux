"""Import pinned primary references for the separate native eeLinux SMAP driver.
SPDX-License-Identifier: GPL-2.0-only
"""
from pathlib import Path
import hashlib
import json
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "kernel/vendor/ethernet"
SOURCES = [
    ("frno7/linux", "f3285c0c4bbfb4b6c97d0d0a261dab92148e1d35", "drivers/ps2/smap.c", "legacy-linux/smap.c"),
    ("frno7/linux", "f3285c0c4bbfb4b6c97d0d0a261dab92148e1d35", "drivers/ps2/smap.h", "legacy-linux/smap.h"),
    ("ps2dev/ps2eth", "791126fccff396518ed17d67122868e8f01f59bb", "README", "ps2eth/README"),
    ("ps2dev/ps2eth", "791126fccff396518ed17d67122868e8f01f59bb", "smap-linux/smap.c", "ps2eth/smap-linux/smap.c"),
    ("ps2dev/ps2eth", "791126fccff396518ed17d67122868e8f01f59bb", "smap-linux/smap.h", "ps2eth/smap-linux/smap.h"),
    ("ps2dev/ps2sdk", "c318b136294ed225863b580e115988b05751cf59", "common/include/smapregs.h", "registers/smapregs.h"),
    ("ps2dev/ps2sdk", "c318b136294ed225863b580e115988b05751cf59", "common/include/dev9regs.h", "registers/dev9regs.h"),
    ("ps2dev/ps2sdk", "c318b136294ed225863b580e115988b05751cf59", "common/include/speedregs.h", "registers/speedregs.h"),
    ("ps2dev/ps2sdk", "c318b136294ed225863b580e115988b05751cf59", "LICENSE", "registers/LICENSE"),
]


def main():
    manifest = []
    old_file = DEST / "manifest.json"
    previous = {p["path"]: p["sha256"] for p in json.loads(old_file.read_text())["files"]} if old_file.exists() else {}
    for repo, commit, path, target in SOURCES:
        url = f"https://raw.githubusercontent.com/{repo}/{commit}/{path}"
        data = urllib.request.urlopen(url, timeout=60).read()
        digest = hashlib.sha256(data).hexdigest()
        if previous and previous.get(target) != digest:
            raise RuntimeError(f"Pinned source changed: {url}")
        dest = DEST / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        manifest.append({"path": target, "repository": f"https://github.com/{repo}",
                         "commit": commit, "upstream_path": path, "url": url, "sha256": digest})
    old_file.write_text(json.dumps({"status": "immutable reference sources for the separate eeLinux native SMAP implementation; hardware packet acceptance pending",
                                   "files": manifest}, indent=2) + "\n")
    print(f"Imported {len(manifest)} pinned Ethernet reference files")


if __name__ == "__main__":
    main()
