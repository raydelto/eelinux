"""Build isolated GPL IOP DEV9/SMAP firmware without changing the pinned import.

SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

from iop_firmware import MODULES, validate_firmware

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "kernel/vendor/iopmod"
OVERLAY = ROOT / "kernel/iop-overlay"
REPLACEMENTS = ("module/dev9.c", "builtin/spd-irq.c", "module/smap-service.h")
SHARED_HEADERS = ("ps2-smap-core.h", "ps2-smap-regs.h", "ps2-smap-rpc.h")
REVISION = "12cdf14b78ed5e648ffe1d52e6754f00b3b69001"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def recipe_hashes() -> dict[str, str]:
    recipe = {"scripts/iop_dev9.py": digest(Path(__file__))}
    for path in sorted((ROOT / "kernel/iop-compat").rglob("*")):
        if path.is_file():
            if path.is_symlink():
                raise RuntimeError(f"IOP compatibility header must be regular: {path}")
            recipe[path.relative_to(ROOT).as_posix()] = digest(path)
    for name in REPLACEMENTS:
        path = OVERLAY / name
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"Missing regular IOP overlay: {path}")
        recipe["kernel/iop-overlay/" + name] = digest(path)
    for name in SHARED_HEADERS:
        path = ROOT / "kernel/new-drivers/ps2-smap" / name
        recipe[path.relative_to(ROOT).as_posix()] = digest(path)
    return recipe


def allowed_work_path(path: Path) -> None:
    if path.is_relative_to(VENDOR.resolve()) or path.is_relative_to(OVERLAY.resolve()):
        raise RuntimeError("IOP work/output must stay outside the preserved source directories")


def linker_source(original: str) -> str:
    """Make file/BSS boundaries and MODLOAD's text/data sizes word-safe."""
    before = '''\t\tKEEP(*(.export.*))
\t} :text = 0

\t.rodata : ALIGN(16) {
\t\tKEEP(*(.module.id))
\t\tKEEP(*(.module.id_name))
\t\t*(.rodata)
\t\t*(.rodata.*)
\t} = 0'''
    after = '''\t\tKEEP(*(.export.*))
\t\t. = ALIGN(16);
\t\tKEEP(*(.module.id))
\t\tKEEP(*(.module.id_name))
\t\t*(.rodata)
\t\t*(.rodata.*)
\t\t. = ALIGN(16);
\t} :text = 0'''
    if original.count(before) != 1 or original.count('elf32-tradlittlemips') != 1:
        raise RuntimeError('Review pristine IOP linker section boundaries')
    text = original.replace(before, after).replace('elf32-tradlittlemips','elf32-littlemips')
    for before, after in (
        ('LONG (SIZEOF(.text));', 'LONG (ADDR(.data));'),
        ('LONG (SIZEOF(.data));', 'LONG (ADDR(.bss) - ADDR(.data));'),
        ('\t\t*(.data.*)\n\t} = 0', '\t\t*(.data.*)\n\t\t. = ALIGN(16);\n\t} = 0'),
        ('\t\t*(COMMON)\n\t}', '\t\t*(COMMON)\n\t\t. = ALIGN(16);\n\t}')):
        if text.count(before) != 1: raise RuntimeError('Review IOP data/BSS alignment')
        text = text.replace(before, after)
    return text


def validate_irx_layout(path: Path) -> None:
    """ROM clears BSS with word stores; section sizes must cover all load bytes."""
    data = path.read_bytes()
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    programs = [struct.unpack_from('<8I', data, header[5]+i*header[9]) for i in range(header[10])]
    loads = [p for p in programs if p[0] == 1]
    infos = [p for p in programs if p[0] == 0x70000080]
    if len(loads) != 1 or len(infos) != 1:
        raise RuntimeError('IOP layout requires one load and one module-info segment')
    load, info = loads[0], infos[0]
    if info[4] < 26 or info[1]+info[4] > len(data):
        raise RuntimeError('IOP layout module info is truncated')
    text, initialized, bss = struct.unpack_from('<3I', data, info[1]+12)
    if (load[2] != 0 or load[1] & 3 or load[4] & 15 or load[5] & 15 or
            text + initialized != load[4] or text + initialized + bss != load[5]):
        raise RuntimeError(f'IOP layout has unsafe file/BSS alignment or module sizes: {path.name}')


def irqrelay_source(original: str) -> str:
    anchor = "static enum module_init_status irqrelay_init(int argc, char *argv[])"
    ready = '\tpr_info("irqrelay: IRQ relay ready\\n");'
    if original.count(anchor) != 1 or original.count(ready) != 1:
        raise RuntimeError("Unexpected pristine IRQ relay source")
    callback = r'''/* Fixed-size rearm ABI; ROM dispatch clears packet_size before callbacks. */
#define SMAP_REARM_CMD 0x8000001bu
#define SMAP_REARM_TAG 0x534d0104u
static void smap_rearm(const struct sif_cmd_header *header, void *arg)
{
	const u32 *payload = sif_cmd_payload(header);
	unsigned int irq, flags;
	u32 mask;
	if (header->opt != SMAP_REARM_TAG) return;
	mask = *payload;
	if (mask & ~0x7cu) return;
	irq_save(flags);
	for (irq = IRQ_IOP_SPD_TXDNV; irq <= IRQ_IOP_SPD_EMAC3; irq++) {
		if (find_map(irq) && (mask & (1u << (irq - IRQ_IOP_SPD_BASE))))
			enable_irq(irq);
	}
	irq_restore(flags);
}

'''
    # Entry runs inside ROM LOADFILE. Never wait for an EE logging DMA there.
    # The EE owns a 64-byte mailbox until LOADFILE returns; detach it before
    # returning so a subsequently scheduled worker cannot write freed memory.
    progress = r'''struct relay_progress { u32 magic, phase; s32 result; u32 thread; };
static volatile struct relay_progress *startup_progress;

static void relay_phase(u32 phase, s32 result)
{
	volatile struct relay_progress *p = startup_progress;
	if (!p) return;
	p->result = result;
	p->thread = rpc_stid;
	__asm__ volatile ("" ::: "memory");
	p->phase = phase;
}

static void relay_progress_arg(int argc, char *argv[])
{
	unsigned int i, j;
	static const char prefix[] = "ps2-progress=";
	startup_progress = NULL;
	if (argc <= 0 || argc > 16 || !argv) return;
	for (i = 0; i < (unsigned int)argc; i++) {
		const char *s = argv[i];
		u32 addr = 0;
		if (!s) continue;
		for (j = 0; prefix[j] && s[j] == prefix[j]; j++);
		if (prefix[j]) continue;
		s += j;
		for (j = 0; j < 8; j++) {
			unsigned int digit;
			if (s[j] >= '0' && s[j] <= '9') digit = s[j] - '0';
			else if (s[j] >= 'a' && s[j] <= 'f') digit = s[j] - 'a' + 10;
			else break;
			addr = (addr << 4) | digit;
		}
		if (j != 8 || s[8] || (addr & 15) || addr < 0x10000 || addr > 0x200000 - 64)
			continue;
		startup_progress = (volatile struct relay_progress *)addr;
		if (startup_progress->magic != 0x45454952) startup_progress = NULL;
		return;
	}
}

'''
    text = original.replace("static void irqrelay_rpc_server(void *arg)",
                            progress + "static void irqrelay_rpc_server(void *arg)")
    text = text.replace('\tpr_debug("%s: Calling sifcmd_rpc_loop\\n", __func__);',
                        '\trelay_phase(9, 0);')
    text = text.replace(anchor, callback + anchor)
    text = text.replace("\t\t.stacksize = 1024,", "\t\t.stacksize = 4096,")
    text = text.replace("\trpc_stid = thbase_create(&th);",
        "\trelay_progress_arg(argc, argv);\n\trelay_phase(1, 0);\n"
        "\trelay_phase(2, 0);\n\trpc_stid = thbase_create(&th);")
    text = text.replace('\t\tpr_err("%s: thbase_create failed with %d: %s\\n",\n'
        '\t\t\t__func__, rpc_stid, iop_error_message(rpc_stid));',
        '\t\trelay_phase(8, rpc_stid);')
    text = text.replace("\tsifcmd_set_rpc_queue(&rpc_qdata, rpc_stid);",
        "\trelay_phase(3, 0);\n\tsifcmd_set_rpc_queue(&rpc_qdata, rpc_stid);\n\trelay_phase(4, 0);")
    text = text.replace("\tioperr = thbase_start(rpc_stid, NULL);",
        "\trelay_phase(5, 0);\n\tioperr = thbase_start(rpc_stid, NULL);")
    text = text.replace('\t\tpr_err("%s: thbase_start failed with %d: %s\\n",\n'
        '\t\t\t__func__, ioperr, iop_error_message(ioperr));',
        '\t\trelay_phase(8, ioperr);')
    text = text.replace(ready,
        "\trelay_phase(6, 0);\n\tsif_request_cmd(SMAP_REARM_CMD, smap_rearm, NULL);\n"
        "\trelay_phase(7, 0);\n\tstartup_progress = NULL;")
    text = text.replace("err_create:\n\treturn MODULE_EXIT;",
                        "err_create:\n\tstartup_progress = NULL;\n\treturn MODULE_EXIT;")
    return text


def verify_vendor() -> dict:
    manifest = json.loads((VENDOR / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("revision") != REVISION:
        raise RuntimeError("Unexpected pinned IOP firmware revision")
    for item in manifest["files"]:
        path = VENDOR / "snapshot" / item["path"]
        if (path.is_symlink() or not path.is_file() or path.stat().st_size != item["bytes"]
                or digest(path) != item["sha256"]):
            raise RuntimeError(f"Pristine IOP source mismatch: {path}")
    return manifest


def prepare_source(destination: Path) -> dict:
    """Create a fresh source tree, with all upstream and adapted bytes recorded."""
    destination = Path(destination).resolve()
    allowed_work_path(destination)
    if destination.exists():
        raise RuntimeError(f"Refusing existing IOP source work directory: {destination}")
    manifest = verify_vendor()
    recipe = recipe_hashes()
    shutil.copytree(VENDOR / "snapshot", destination)
    for name in REPLACEMENTS:
        (destination / name).write_bytes((OVERLAY / name).read_bytes())
    for name in SHARED_HEADERS:
        (destination / "include/iopmod" / name).write_bytes((ROOT / "kernel/new-drivers/ps2-smap" / name).read_bytes())
    irqrelay = destination / "module/irqrelay.c"
    irqrelay.write_bytes(irqrelay_source(irqrelay.read_text()).encode())
    dev9_header = destination / "include/iopmod/module/dev9.h"
    original = dev9_header.read_text()
    if original.count("MODULE_ID(dev9, 0x0100);") != 1:
        raise RuntimeError("Unexpected DEV9 module version header")
    dev9_header.write_bytes(original.replace("MODULE_ID(dev9, 0x0100);", "MODULE_ID(dev9, 0x0102);").encode())
    # Current ps2dev binutils use elf32-littlemips rather than the old BFD name.
    linker = destination / "script/iop.ld"
    linker_text = linker.read_text()
    if linker_text.count("elf32-tradlittlemips") != 1:
        raise RuntimeError("Unexpected IOP linker template")
    (destination / ".eelinux-iop.ld").write_bytes(linker_source(linker_text).encode())
    report = {
        "schema": 1, "source": {"repository": manifest["repository"], "revision": REVISION},
        "source_manifest_sha256": digest(VENDOR / "manifest.json"),
        "recipe_sha256": recipe,
        "adapted_source_sha256": {name: digest(destination / name) for name in
            (*REPLACEMENTS, *("include/iopmod/" + name for name in SHARED_HEADERS), "module/irqrelay.c", "include/iopmod/module/dev9.h", ".eelinux-iop.ld")},
        "module_versions": {"dev9": "0x0102"}, "dev9_library_version": "0x0100",
        "transport": {"dev9_command": "0x8000001c", "rearm_command": "0x8000001b"},
    }
    (destination / ".eelinux-source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def validate_overlay_firmware(directory: Path) -> dict:
    """Validate six IRX and reproduce their recorded source recipe identity."""
    directory = Path(directory)
    for path in validate_firmware(directory):
        validate_irx_layout(path)
    verify_vendor()
    report = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if (report.get("source") != {"repository": "https://github.com/frno7/iopmod.git", "revision": REVISION}
            or report.get("source_manifest_sha256") != digest(VENDOR / "manifest.json")
            or report.get("recipe_sha256") != recipe_hashes()
            or report.get("module_versions") != {"dev9": "0x0102"}
            or report.get("dev9_library_version") != "0x0100"
            or report.get("transport") != {"dev9_command": "0x8000001c", "rearm_command": "0x8000001b"}):
        raise RuntimeError("DEV9 firmware source/recipe provenance mismatch")
    originals = VENDOR / "snapshot"
    texts = {name: (OVERLAY / name).read_bytes() for name in REPLACEMENTS}
    texts.update({"include/iopmod/" + name: (ROOT / "kernel/new-drivers/ps2-smap" / name).read_bytes() for name in SHARED_HEADERS})
    texts["module/irqrelay.c"] = irqrelay_source((originals / "module/irqrelay.c").read_text()).encode()
    texts["include/iopmod/module/dev9.h"] = (originals / "include/iopmod/module/dev9.h").read_text().replace(
        "MODULE_ID(dev9, 0x0100);", "MODULE_ID(dev9, 0x0102);").encode()
    texts[".eelinux-iop.ld"] = linker_source((originals / "script/iop.ld").read_text()).encode()
    if report.get("adapted_source_sha256") != {name: hashlib.sha256(data).hexdigest() for name, data in texts.items()}:
        raise RuntimeError("DEV9 firmware adapted source identity mismatch")
    if report.get("firmware_bytes") != {path.name: path.stat().st_size for path in validate_firmware(directory)}:
        raise RuntimeError("DEV9 firmware size manifest mismatch")
    # Do not accept an old DEV9 IRX merely because a manifest calls it 0x0101.
    data = (directory / "dev9.irx").read_bytes()
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    for index in range(header[10]):
        segment = struct.unpack_from("<8I", data, header[5] + index * header[9])
        if segment[0] == 0x70000080:
            info = data[segment[1]:segment[1] + segment[4]]
            if len(info) < 31 or struct.unpack_from("<H", info, 24)[0] != 0x0102 or info[26:].split(b"\0", 1)[0] != b"dev9":
                raise RuntimeError("DEV9 IRX has the wrong actual module name/version")
    export = struct.pack("<III8s", 0x41c00000, 0, 0x0100, b"dev9")
    if export not in data:
        raise RuntimeError("DEV9 IRX must retain the actual ATA-compatible library export")
    return report


def build(work: Path, destination: Path, cross_prefix: str, jobs: int) -> dict:
    if os.name != "posix":
        raise RuntimeError("Build IOP firmware on Linux/WSL")
    if jobs < 1 or jobs > 2:
        raise RuntimeError("Use one or two IOP build jobs")
    for tool in ("gcc", "ld", "objcopy", "objdump"):
        if not shutil.which(cross_prefix + tool):
            raise RuntimeError(f"Missing ps2dev IOP tool: {cross_prefix + tool}")
    destination = Path(destination).resolve()
    allowed_work_path(destination)
    if destination.exists():
        raise RuntimeError(f"Refusing existing firmware output: {destination}")
    report = prepare_source(work)
    work = Path(work).resolve()
    formats = subprocess.check_output([cross_prefix + "objdump", "-i"], text=True)
    linker = work / ".eelinux-iop.ld"
    if "elf32-littlemips" not in formats:
        raise RuntimeError("Unsupported ps2dev IOP ELF format")
    # The pinned IRX linker/import scaffolding is qualified at -O0 only.
    # An actual O2/O0 boot comparison found optimized modules returning
    # MODULE_EXIT (printk, DEV9 and IRQ), with identical source/imports.
    # Preserve upstream's conservative setting until optimized IRX startup
    # and relocation behavior have independent loader qualification.
    flags = ("-O0 -march=r3000 -EL -msoft-float -fomit-frame-pointer "
             "-fno-pic -mno-abicalls -fno-common -ffreestanding -static "
             "-fno-strict-aliasing -nostdlib -mlong-calls -mno-gpopt "
             "-mno-shared -G0 -ffunction-sections -fdata-sections "
             f"-Wall -Iinclude -I{ROOT / 'kernel/iop-compat'}")
    subprocess.run(["make", "-C", str(work), f"-j{jobs}", f"CROSS_COMPILE={cross_prefix}",
                    f"IOP_CFLAGS={flags}", f"MODULE_LD={linker}"], check=True)
    for name, expected in report["adapted_source_sha256"].items():
        if digest(work / name) != expected:
            raise RuntimeError(f"IOP source changed during build: {name}")
    pristine = json.loads((VENDOR / "manifest.json").read_text(encoding="utf-8"))
    for item in pristine["files"]:
        if item["path"] not in report["adapted_source_sha256"] and digest(work / item["path"]) != item["sha256"]:
            raise RuntimeError(f"Pristine staged IOP source changed during build: {item['path']}")
    for name, expected in report["recipe_sha256"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError(f"IOP recipe changed during build: {name}")
    verify_vendor()
    destination.mkdir(parents=True)
    report["compiler"] = cross_prefix
    report["compiler_version"] = subprocess.check_output([cross_prefix + "gcc", "--version"], text=True).splitlines()[0]
    report["compiler_flags"] = flags
    report["firmware_sha256"] = {}
    report["firmware_bytes"] = {}
    for name in MODULES:
        image = destination / (name + ".irx")
        shutil.copyfile(work / "module" / image.name, image)
        report["firmware_sha256"][image.name] = digest(image)
        report["firmware_bytes"][image.name] = image.stat().st_size
    (destination / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    validate_overlay_firmware(destination)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--cross-prefix", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    build(args.work, args.destination, args.cross_prefix, args.jobs)


if __name__ == "__main__":
    main()
