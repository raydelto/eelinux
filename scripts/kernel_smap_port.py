"""Install the reviewed ps2 native SMAP source without altering the API patch.

The driver patch is separate from the incomplete R5900 core adaptation. It
does not close the source gate or establish hardware acceptance.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "kernel/new-drivers/ps2-smap"
FILES = ("Kconfig", "Makefile", "ps2-smap.c", "ps2-smap-core.h", "ps2-smap-regs.h",
         "ps2-smap-iop.c", "ps2-smap-iop.h", "ps2-smap-rpc.h", "ps2-smap-phy.h")
BRIDGE_FILES = ("arch/mips/ps2/Kconfig", "drivers/ps2/Makefile", "drivers/ps2/sif.c",
                "arch/mips/include/asm/mach-ps2/sif.h", "drivers/ps2/iop-heap.c",
                "arch/mips/include/asm/mach-ps2/iop-heap.h", "drivers/ps2/iop-module-request.c")


def replace_once(text: str, old: str, new: str, context: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError(f"Unexpected {context}; review built-in IOP initialization")
    return text.replace(old, new, 1)


def module_start_guard(text: str) -> str:
    """Cache IRX exports only after a supported resident start result."""
    text = replace_once(text, '''/**
 * iop_module_link_buffer - link IOP module given in buffer''', '''/* An exported library is usable only while its providing module is resident. */
static bool iop_module_has_exports(const struct elf32_hdr *ehdr)
{
	const struct irx_export_library *library;

	irx_for_each_export_library (library, ehdr)
		return true;
	return false;
}

/**
 * iop_module_link_buffer - link IOP module given in buffer''', 'IOP export scan')
    text = replace_once(text, '''	} result;
	int err;

	BUILD_BUG_ON(sizeof(link) != 512);''', '''	} result = { .status = -EIO, .modres = ~0U };
	int err;

	BUILD_BUG_ON(sizeof(link) != 512);''', 'IOP module reply initialization')
    text = replace_once(text, '''
	err = register_libraries(buf);
''', '''
	/* Module entry results: 0 resident, 1 exit, 2 removable resident.
	 * An exited exporter must not satisfy later library dependencies.
	 * Unknown or failed start results are not trusted as providers.
	 */
	if (result.modres > 2 ||
	    (result.modres == 1 && iop_module_has_exports(buf))) {
		pr_err("iop-module: %s: unsupported/nonresident start result %#x; refusing library cache\\n",
			__func__, result.modres);
		err = -ENOEXEC;
		goto err_out;
	}

	err = register_libraries(buf);
''', 'IOP resident module cache guard')
    text = replace_once(text, ''' * As a simplification the IOP module linker assumes that modules with
 * exported libraries are resident. The primary effect is that it refuses to''', ''' * The IOP module linker checks the module entry result before caching
 * exported libraries. Modules reporting exit or an unsupported start result
 * cannot provide exports. For resident modules, the linker refuses to''', 'IOP residency documentation')
    return text


def bridge_changes(inputs: dict[str, str]) -> dict[str, str]:
    """Preserve modular USB-only builds and provide ordered built-in SMAP deps."""
    result = dict(inputs)
    path = "arch/mips/ps2/Kconfig"
    if "config PS2_IOP_BRIDGE" in result[path] or "config PS2_GS\n" not in result[path]:
        raise RuntimeError("Unexpected PS2 platform Kconfig; do not duplicate IOP bridge")
    result[path] = result[path].rstrip() + '''

config PS2_IOP_BRIDGE
	tristate "PlayStation 2 SIF/IOP bridge"
	depends on SONY_PS2
	default m
	help
	  SIF commands, IOP memory/heap, module loading and interrupt routing.
	  Native built-in SMAP selects this built in as well. Initialization is
	  ordered SIF, heap, module service, then IRQs; failed services reject
	  later requests instead of using an uninitialized RPC client.
'''
    path = "drivers/ps2/Makefile"
    for obj in ("sif", "iop-memory", "iop-heap", "iop-module", "iop-irq"):
        pattern = rf"^obj-m\s*\+= {obj}\.o\n"
        if len(re.findall(pattern, result[path], re.M)) != 1:
            raise RuntimeError(f"Unexpected hard-coded IOP object: {obj}")
        result[path] = re.sub(pattern, "", result[path], flags=re.M)
    block = "".join(f"obj-$(CONFIG_PS2_IOP_BRIDGE) += {obj}.o\n" for obj in
                    ("iop-memory", "sif", "iop-heap", "iop-module", "iop-irq")) + "\n"
    result[path] = replace_once(result[path], "iop-module-objs", block + "iop-module-objs", path)
    path = "drivers/ps2/sif.c"
    result[path] = replace_once(result[path], "static int __init sif_init(void)",
        '''static bool sif_ready;

bool sif_is_ready(void)
{
	return READ_ONCE(sif_ready);
}
EXPORT_SYMBOL_GPL(sif_is_ready);

static int __init sif_init(void)''', path)
    result[path] = replace_once(result[path], "\n\treturn 0;\n\nerr_rpc_init:",
                                "\n\tWRITE_ONCE(sif_ready, true);\n\treturn 0;\n\nerr_rpc_init:", path)
    result[path] = replace_once(result[path], "static void __exit sif_exit(void)\n{",
                                "static void __exit sif_exit(void)\n{\n\tWRITE_ONCE(sif_ready, false);", path)
    path = "arch/mips/include/asm/mach-ps2/sif.h"
    result[path] = replace_once(result[path], "int sif_request_cmd(u32 cmd, sif_cmd_cb cb, void *arg);",
                                "bool sif_is_ready(void);\n\nint sif_request_cmd(u32 cmd, sif_cmd_cb cb, void *arg);", path)
    path = "drivers/ps2/iop-heap.c"
    result[path] = replace_once(result[path], "static struct sif_rpc_client iop_heap_rpc;",
        '''static struct sif_rpc_client iop_heap_rpc;
static bool iop_heap_ready;

bool iop_heap_is_ready(void)
{
	return READ_ONCE(iop_heap_ready);
}
EXPORT_SYMBOL_GPL(iop_heap_is_ready);''', path)
    result[path] = replace_once(result[path],
        '''static int __init iop_heap_init(void)
{
	return sif_rpc_bind(&iop_heap_rpc, SIF_SID_HEAP);
}''', '''static int __init iop_heap_init(void)
{
	int err;
	if (!sif_is_ready())
		return -ENODEV;
	err = sif_rpc_bind(&iop_heap_rpc, SIF_SID_HEAP);
	WRITE_ONCE(iop_heap_ready, !err);
	return err;
}''', path)
    result[path] = replace_once(result[path], "static void __exit iop_heap_exit(void)\n{",
                                "static void __exit iop_heap_exit(void)\n{\n\tWRITE_ONCE(iop_heap_ready, false);", path)
    path = "arch/mips/include/asm/mach-ps2/iop-heap.h"
    result[path] = replace_once(result[path], "iop_addr_t iop_alloc(size_t nbyte);",
                                "bool iop_heap_is_ready(void);\n\niop_addr_t iop_alloc(size_t nbyte);", path)
    path = "drivers/ps2/iop-module-request.c"
    result[path] = replace_once(result[path], "static struct sif_rpc_client load_file_rpc_client;",
                                "static struct sif_rpc_client load_file_rpc_client;\nstatic bool iop_module_ready;", path)
    result[path] = replace_once(result[path],
        '''int iop_module_request(const char *name, int version, const char *arg)
{
	int err;

	mutex_lock(&iop_module_lock);''', '''int iop_module_request(const char *name, int version, const char *arg)
{
	int err;

	if (!READ_ONCE(iop_module_ready))
		return -ENODEV;
	mutex_lock(&iop_module_lock);''', path)
    result[path] = replace_once(result[path],
        '''static int __init iop_module_init(void)
{
	int err;

	err = iop_module_patch();''', '''static int __init iop_module_init(void)
{
	int err;

	if (!iop_heap_is_ready())
		return -ENODEV;
	err = iop_module_patch();''', path)
    result[path] = replace_once(result[path], "\n\treturn 0;\n\nerr_printk:",
                                "\n\tWRITE_ONCE(iop_module_ready, true);\n\treturn 0;\n\nerr_printk:", path)
    result[path] = module_start_guard(result[path])
    from kernel_smap_iop_port import adapt_rpc, adapt_header
    result["drivers/ps2/sif.c"] = adapt_rpc(result["drivers/ps2/sif.c"])
    result["arch/mips/include/asm/mach-ps2/sif.h"] = adapt_header(result["arch/mips/include/asm/mach-ps2/sif.h"])
    return result


def driver_changes(kconfig: str, makefile: str, installed: bool = False) -> dict[str, str]:
    """Accept one normal Ethernet menu; never duplicate or overwrite a driver."""
    if installed or "PS2_SMAP" in makefile or "ethernet/ps2/Kconfig" in kconfig:
        raise RuntimeError("Native SMAP already installed; preserve its source/report")
    anchor = "endif # ETHERNET\n"
    if kconfig.count(anchor) != 1 or "drivers/net/ethernet/sony/Kconfig" in kconfig:
        raise RuntimeError("Unexpected Ethernet Kconfig; review the registration anchor")
    return {
        "drivers/net/ethernet/Kconfig": kconfig.replace(
            anchor, 'source "drivers/net/ethernet/ps2/Kconfig"\n\n' + anchor, 1),
        "drivers/net/ethernet/Makefile": makefile.rstrip() +
            "\nobj-$(CONFIG_PS2_SMAP) += ps2/\n",
        **{"drivers/net/ethernet/ps2/" + name: (DRIVER / name).read_text()
           for name in FILES},
    }


def install(source: Path, patch_output: Path) -> dict:
    source = source.resolve()
    text = (source / "Makefile").read_text()
    version = ".".join(re.search(rf"^{key}\s*=\s*(\d+)$", text, re.M).group(1)
                       for key in ("VERSION", "PATCHLEVEL", "SUBLEVEL"))
    if version != "7.2.8" or not (source / ".eelinux-modernize.json").is_file():
        raise RuntimeError("Native SMAP needs the reviewed modern PS2 7.2.8 source first")
    original = {path: (source / path).read_text()
                for path in ("drivers/net/ethernet/Kconfig", "drivers/net/ethernet/Makefile", *BRIDGE_FILES)}
    changes = driver_changes(original["drivers/net/ethernet/Kconfig"],
                             original["drivers/net/ethernet/Makefile"],
                             installed=(source / "drivers/net/ethernet/ps2").exists())
    changes.update(bridge_changes({path: original[path] for path in BRIDGE_FILES}))
    lines = []
    for path, content in changes.items():
        lines.extend(difflib.unified_diff(original.get(path, "").splitlines(keepends=True),
                                        content.splitlines(keepends=True),
                                        fromfile="a/" + path if path in original else "/dev/null",
                                        tofile="b/" + path))
    patch_output = patch_output.resolve()
    patch_output.parent.mkdir(parents=True, exist_ok=True)
    patch_output.write_bytes("".join(lines).encode())
    subprocess.run(["git", "apply", "--check", str(patch_output)], cwd=source, check=True)
    subprocess.run(["git", "apply", str(patch_output)], cwd=source, check=True)
    hashes = {path: hashlib.sha256((source / path).read_bytes()).hexdigest() for path in changes}
    for path, content in changes.items():
        if (source / path).read_bytes() != content.encode():
            raise RuntimeError(f"SMAP source differs after patch: {path}")
    report = {"schema": 1, "target": "Linux 7.2.8", "driver": "native PS2 SMAP IOP/SIF",
              "symbol": "PS2_SMAP", "source_sha256": hashes,
              "base_registration_sha256": {path: hashlib.sha256(content.encode()).hexdigest()
                                            for path, content in original.items()},
              "patch_sha256": hashlib.sha256(patch_output.read_bytes()).hexdigest(),
              "required_iop_firmware": "ps2 DEV9/IRQ/IRQrelay native Ethernet overlay",
              "native_smap_source_implemented": True, "hardware_validated": False,
              "production_source_gate_changed": False}
    (source / ".eelinux-smap.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--patch-output", default=ROOT / "kernel/patches/ps2-smap.patch", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = install(args.source, args.patch_output)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
