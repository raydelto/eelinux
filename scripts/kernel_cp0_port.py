"""Keep R5900 EntryHi accesses at the hardware's 32-bit CP0 width.

The EE has 64-bit integer instructions but no DMFC0/DMTC0 instructions.
These guards retain the modern probe/uniquification for other MIPS CPUs.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess

PATHS = ("arch/mips/kernel/cpu-probe.c", "arch/mips/mm/tlb-r4k.c")


def adapt_cpu_probe(text: str) -> str:
    before = "\tif (cpu_has_64bits) {\n\t\twrite_c0_entryhi_64(0x3fffffffffffe000ULL);"
    after = ("\t/* R5900 has 64-bit GPR operations, but only 32-bit CP0 transfers. */\n"
             "\tif (cpu_has_64bits && !IS_ENABLED(CONFIG_CPU_R5900)) {\n"
             "\t\twrite_c0_entryhi_64(0x3fffffffffffe000ULL);")
    if text.count(before) != 1 or "!IS_ENABLED(CONFIG_CPU_R5900)" in text:
        raise RuntimeError("Review the virtual-address probe before adapting R5900 CP0")
    return text.replace(before, after, 1)


def adapt_tlb(text: str) -> str:
    pairs = (
        ("\treturn cpu_has_64bits ? read_c0_entryhi_64() : read_c0_entryhi();",
         "\treturn cpu_has_64bits && current_cpu_type() != CPU_R5900 ?\n"
         "\t\tread_c0_entryhi_64() : read_c0_entryhi();"),
        ("\tif (cpu_has_64bits)\n\t\twrite_c0_entryhi_64(v);",
         "\tif (cpu_has_64bits && current_cpu_type() != CPU_R5900)\n"
         "\t\twrite_c0_entryhi_64(v);"),
        ("\tvpn_mask |= cpu_has_64bits ? 3ULL << 62 : 1 << 31;",
         "\tvpn_mask |= cpu_has_64bits && current_cpu_type() != CPU_R5900 ?\n"
         "\t\t3ULL << 62 : 1ULL << 31;"),
    )
    if "current_cpu_type() != CPU_R5900" in text or any(text.count(a) != 1 for a, _ in pairs):
        raise RuntimeError("Review TLB EntryHi accesses and VPN mask before adapting R5900 CP0")
    text = text.replace("/* Read full EntryHi even with CONFIG_32BIT.  */",
                        "/* R5900 EntryHi is 32-bit even though its integer ISA is 64-bit. */\n"
                        "/* Read full EntryHi even with CONFIG_32BIT on other CPUs. */", 1)
    for before, after in pairs:
        text = text.replace(before, after, 1)
    return text


def install(source: Path, patch: Path) -> dict:
    source = source.resolve(strict=True)
    marker = source / ".eelinux-cp0.json"
    if marker.exists():
        report = json.loads(marker.read_text())
        if (report["patch_sha256"] != hashlib.sha256(patch.read_bytes()).hexdigest()
                or any(hashlib.sha256((source / p).read_bytes()).hexdigest() != h
                       for p, h in report["source_sha256"].items())):
            raise RuntimeError("R5900 CP0 source/patch provenance mismatch")
        return report
    changes = {}
    for path, adapter in zip(PATHS, (adapt_cpu_probe, adapt_tlb)):
        before = (source / path).read_text()
        changes[path] = (before, adapter(before))
    data = "".join("".join(difflib.unified_diff(a.splitlines(keepends=True), b.splitlines(keepends=True),
                    fromfile="a/" + p, tofile="b/" + p)) for p, (a, b) in changes.items()).encode()
    if patch.exists() and patch.read_bytes() != data:
        raise RuntimeError("Preserve the recorded CP0 patch; review differing source")
    patch.parent.mkdir(parents=True, exist_ok=True)
    patch.write_bytes(data)
    subprocess.run(["git", "apply", "--check", str(patch.resolve())], cwd=source, check=True)
    subprocess.run(["git", "apply", str(patch.resolve())], cwd=source, check=True)
    report = {"patch_sha256": hashlib.sha256(data).hexdigest(),
              "source_sha256": {p: hashlib.sha256((source / p).read_bytes()).hexdigest() for p in PATHS},
              "hardware_validated": False, "complete_port": False}
    marker.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(install(args.source, args.patch), indent=2))
