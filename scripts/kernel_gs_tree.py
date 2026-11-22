"""Prepare the native-GS Linux 7.2.8 tree used by the PAL/NTSC media kernels.
SPDX-License-Identifier: GPL-2.0-only

Copies a ported and modernized tree (``make kernel-port kernel-modernize``),
then applies the W=1 warning cleanup and the recorded GS patch series in order.
The patches hold the generated driver state; ``kernel/new-drivers`` holds the
port scripts' inputs, which those scripts transform. The input tree is left
unchanged.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from common import ROOT

# docs/graphics.md "Kernel patches and experimental ABI", then the later
# extensions in the order documented in docs/graphics.md and graphics-alpha.md.
SERIES = ("framebuffer", "commands", "surfaces", "rendering", "textures", "logic",
          "transfer", "readback", "batch",
          "lut", "map", "map-blit", "rgba-transfer", "surface-capacity")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True, help="ported and modernized 7.2.8 tree")
    p.add_argument("--output", type=Path, required=True, help="new native-GS tree (must not exist)")
    a = p.parse_args()
    source = a.source.resolve()
    if not (source / ".eelinux-modernize.json").is_file():
        raise RuntimeError("Run make kernel-port and make kernel-modernize on --source first")
    if a.output.exists():
        raise RuntimeError("Output tree exists; choose a fresh --output")
    shutil.copytree(source, a.output, symlinks=True)
    # W=1 -Werror cleanup that modernize does not apply; media kernels need it.
    for patch in [ROOT / "kernel/patches/ps2-kernel-warning-cleanup.patch",
                  *(ROOT / "kernel/patches" / f"ps2-gs-{name}.patch" for name in SERIES)]:
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=a.output, check=True)
        subprocess.run(["git", "apply", str(patch)], cwd=a.output, check=True)
        print(f"applied {patch.name}")
    print(f"Native GS tree: {a.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"kernel-gs-tree: {exc}", file=sys.stderr)
        sys.exit(1)
