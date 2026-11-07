#!/usr/bin/env python3
"""Forward-port source registration, driver APIs and reviewed PS2 glue.

Only listed compatibility changes are resolved. R5900 exception/TLB errata,
full multimedia context preservation and graphics implementations remain incomplete.
"""
import argparse
import difflib
import json
from pathlib import Path
import re
import subprocess
import sys
from kernel_cache_port import modernize_r4kcache
from kernel_cp0_port import install as install_r5900_cp0
from kernel_user_vm_port import install as install_user_vm
from kernel_video_port import install as install_video
from kernel_page_cache_port import install as install_page_cache
from kernel_fault_port import install as install_ps2_fault
from kernel_iop_upload_port import install as install_iop_upload
from kernel_iop_patch_port import install as install_iop_patch
from kernel_intc_port import install as install_intc
from kernel_iop_irq_port import modernize_iop_irq
from kernel_ohci_irq_port import modernize_ohci_irq
from kernel_smap_port import install as install_native_smap
from kernel_tls_port import modernize_traps_header, modernize_unaligned
from kernel_usb_boot_port import install as install_usb_boot
from kernel_sif_usb_port import install as install_sif_usb

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "kernel/vendor/frno7/snapshot"


def option(text, name):
    match = re.search(rf"^config {name}\n.*?(?=^config |^endchoice|^endif|^endmenu|\Z)",
                      text, re.M | re.S)
    if not match:
        raise RuntimeError(f"Missing original Kconfig block: {name}")
    return match.group(0).rstrip() + "\n\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--patch-output", required=True)
    args = parser.parse_args()
    source = Path(args.source).resolve()
    if not (source / ".eelinux-port.json").is_file():
        raise RuntimeError("Run kernel.py port on the verified 7.2.8 tree first")
    if (source / ".eelinux-modernize.json").exists():
        raise RuntimeError("Modern API adaptation was already attempted; preserve that source and report")
    original = {}
    updated = {}

    def read(path):
        if path not in original:
            original[path] = (source / path).read_text()
            updated[path] = original[path]
        return updated[path]

    def add(path, text, anchor=None):
        current = read(path)
        if anchor is None:
            updated[path] = current.rstrip() + "\n\n" + text
        elif anchor not in current:
            raise RuntimeError(f"Missing modern source anchor in {path}: {anchor}")
        else:
            updated[path] = current.replace(anchor, text + anchor, 1)

    old = (VENDOR / "arch/mips/Kconfig").read_text()
    path = "arch/mips/Kconfig"
    # SONY_PS2 belongs to the existing machine choice, not an appended free bool.
    if "config SONY_PS2\n" not in read(path):
        block = option(old, "SONY_PS2").replace("\tselect NO_IOPORT\n", "\tselect NO_IOPORT_MAP\n")
        block = block.replace("\tselect USB_ARCH_HAS_OHCI\n", "")
        add(path, block, "endchoice\n")
    if 'source "arch/mips/ps2/Kconfig"' not in read(path):
        add(path, 'source "arch/mips/ps2/Kconfig"\n')
    if "config CPU_R5900\n" not in read(path):
        # CPU_NEVADA exists in the modern processor choice.
        add(path, option(old, "CPU_R5900"), "config CPU_NEVADA\n")
    if "config SYS_HAS_CPU_R5900\n" not in read(path):
        add(path, "config SYS_HAS_CPU_R5900\n\tbool\n\n", "config SYS_HAS_CPU_NEVADA\n")
    current = read(path)
    updated[path] = current.replace("default y if CPU_R3000 || CPU_TX39XX\n",
                                   "default y if CPU_R3000 || CPU_TX39XX || CPU_R5900\n")
    if "platform-$(CONFIG_SONY_PS2)" not in read("arch/mips/Kbuild.platforms"):
        add("arch/mips/Kbuild.platforms", "platform-$(CONFIG_SONY_PS2) += ps2/\n\n",
            "# include the platform specific files")
    path = "arch/mips/ps2/Platform"
    updated[path] = re.sub(r'^platform-.*\n', '', read(path), flags=re.M)
    if "cflags-$(CONFIG_CPU_R5900)" not in read("arch/mips/Makefile"):
        add("arch/mips/Makefile", "cflags-$(CONFIG_CPU_R5900) += -march=r5900 -Wa,--trap\n",
            "cflags-$(CONFIG_CPU_NEVADA)")
    path = "arch/mips/include/asm/cpu.h"
    text = read(path)
    if "PRID_IMP_R5900" not in text:
        add(path, "#define PRID_IMP_R5900 0x2e00 /* PlayStation 2 */\n", "#define PRID_IMP_R5000")
    text = read(path)
    if not re.search(r"\bCPU_R5900\b", text):
        if "CPU_SR71000, CPU_TX49XX," not in text:
            raise RuntimeError("Inspect the modern cpu_type_enum before placing R5900")
        updated[path] = text.replace("CPU_SR71000, CPU_TX49XX,", "CPU_SR71000, CPU_TX49XX, CPU_R5900,", 1)

    registrations = [
        ("drivers/ata", "PATA_PS2", "pata_ps2.o"),
        ("drivers/rtc", "RTC_DRV_PS2", "rtc-ps2.o"),
        ("drivers/usb/host", "USB_OHCI_HCD_PS2", "ohci-ps2.o"),
        ("drivers/tty/serial", "SERIAL_PS2_UART", "ps2_uart.o"),
    ]
    for directory, symbol, obj in registrations:
        kpath = directory + "/Kconfig"
        if f"config {symbol}\n" not in read(kpath):
            block = option((VENDOR / kpath).read_text(), symbol)
            if symbol == "RTC_DRV_PS2":
                add(kpath, block, "endif # RTC_CLASS")
            else:
                add(kpath, block)
        mpath = directory + "/Makefile"
        if symbol not in read(mpath):
            add(mpath, f"obj-$(CONFIG_{symbol}) += {obj}\n")
    path = "drivers/tty/serial/Kconfig"
    if "config SERIAL_PS2_UART_CONSOLE\n" not in read(path):
        add(path, option((VENDOR / path).read_text(), "SERIAL_PS2_UART_CONSOLE"))
    if "CONFIG_SONY_PS2" not in read("drivers/Makefile") or "ps2/" not in read("drivers/Makefile"):
        add("drivers/Makefile", "obj-$(CONFIG_SONY_PS2) += ps2/\n")
    if 'source "drivers/gpu/drm/gs/Kconfig"' not in read("drivers/gpu/drm/Kconfig"):
        add("drivers/gpu/drm/Kconfig", 'source "drivers/gpu/drm/gs/Kconfig"\n')
    if "CONFIG_DRM_GS" not in read("drivers/gpu/drm/Makefile"):
        add("drivers/gpu/drm/Makefile", "obj-$(CONFIG_DRM_GS) += gs/\n")

    # The custom UART number in the old fork collides with modern public types.
    path = "include/uapi/linux/serial_core.h"
    if "PORT_PS2_UART" not in read(path):
        values = [int(v) for v in re.findall(r"^#define\s+PORT_\w+\s+(\d+)", read(path), re.M)]
        number = max(values) + 1
        add(path, f"/* PlayStation 2 EE UART, experimental downstream type. */\n#define PORT_PS2_UART {number}\n",
            "#endif /* _UAPILINUX_SERIAL_CORE_H */")

    # Modern platform remove callbacks cannot return an error.
    for path, function in [
        ("drivers/ps2/gs.c", "gs_remove"),
        ("drivers/ps2/iop-module-sysfs.c", "iop_remove"),
        ("drivers/ata/pata_ps2.c", "pata_ps2_remove"),
        ("drivers/tty/serial/ps2_uart.c", "ps2_uart_remove"),
        ("drivers/usb/host/ohci-ps2.c", "ohci_hcd_ps2_remove"),
    ]:
        text = read(path)
        marker = f"static int {function}("
        if marker not in text:
            raise RuntimeError(f"Missing expected imported remove callback: {path}")
        start = text.index(marker)
        end = text.index("\n}\n", start) + 3
        body = text[start:end].replace(marker, f"static void {function}(", 1).replace("return 0;", "return;")
        updated[path] = text[:start] + body + text[end:]
    path = "drivers/video/fbdev/ps2fb.c"
    text = read(path)
    start = text.index("static int ps2fb_remove(")
    end = text.index("\n}\n", start) + 3
    body = text[start:end].replace("static int ps2fb_remove(", "static void ps2fb_remove(", 1)
    body = body.replace("\tint err = 0;\n", "").replace("\t\terr = -EBUSY;\n", "")
    body = body.replace("return 0;", "return;").replace("return err;", "return;")
    updated[path] = text[:start] + body + text[end:]
    path = "drivers/ata/pata_ps2.c"
    text = read(path)
    # ata_noop_qc_prep was removed when qc_prep acquired an error return.
    # Preserve this bridge's no-preparation behavior with an explicit callback.
    helper = ("static enum ata_completion_errors pata_ps2_qc_prep(struct ata_queued_cmd *qc)\n"
              "{\n\treturn 0;\n}\n\n")
    anchor = "static struct ata_port_operations pata_ps2_port_ops"
    if anchor not in text or "ata_noop_qc_prep" not in text:
        raise RuntimeError("Inspect imported PS2 ATA command preparation before adapting")
    updated[path] = text.replace(anchor, helper + anchor, 1).replace("ata_noop_qc_prep", "pata_ps2_qc_prep")

    path = "drivers/tty/serial/ps2_uart.c"
    text = read(path)
    text = text.replace("struct ktermios *termios, struct ktermios *old)",
                        "struct ktermios *termios, const struct ktermios *old)")
    text = text.replace("from_timer(pp, t, timer)", "timer_container_of(pp, t, timer)")
    text = text.replace("del_timer_sync(", "timer_delete_sync(")
    start = text.index("static void ps2_uart_tx_chars(")
    end = text.index("\n}\n", start) + 3
    tx = '''static void ps2_uart_tx_chars(struct uart_port *port)
{
\tstruct tty_port *tport = &port->state->port;
\tunsigned char ch;

\tif (port->x_char) {
\t\tps2_uart_putchar_block(port->x_char);
\t\tport->icount.tx++;
\t\tport->x_char = 0;
\t\treturn;
\t}
\tif (kfifo_is_empty(&tport->xmit_fifo) || uart_tx_stopped(port)) {
\t\tps2_uart_stop_tx(port);
\t\treturn;
\t}

\twhile ((inw(SIO_ISR) & 0xf000) != 0x8000) {
\t\tif (!uart_fifo_get(port, &ch))
\t\t\tbreak;
\t\toutb(ch, SIO_TXFIFO);
\t}

\tif (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
\t\tuart_write_wakeup(port);
\tif (kfifo_is_empty(&tport->xmit_fifo))
\t\tps2_uart_stop_tx(port);
}
'''
    text = text[:start] + tx + text[end:]
    text = text.replace("\tstruct circ_buf *xmit;\n", "")
    text = text.replace("\txmit = &port->state->xmit;\n", "")
    text = text.replace("(uart_circ_chars_pending(xmit) > 0)",
                        "!kfifo_is_empty(&port->state->port.xmit_fifo)")
    updated[path] = text
    path = "drivers/video/fbdev/ps2fb.c"
    text = read(path).replace("#include <linux/of_device.h>\n",
                              "#include <linux/of_device.h>\n#include <linux/platform_device.h>\n")
    text = text.replace("info->flags = FBINFO_DEFAULT |\n", "info->flags =\n")
    text = text.replace("info->pixmap.blit_x =", "info->pixmap.blit_x[0] =")
    text = text.replace("info->pixmap.blit_y =", "info->pixmap.blit_y[0] =")
    text = text.replace("strlcpy(", "strscpy(")
    updated[path] = text
    # RTC registration API was removed: preserve devres ownership with the modern API.
    path = "drivers/rtc/rtc-ps2.c"
    text = read(path)
    old_call = 'rtc = devm_rtc_device_register(&pdev->dev,\n\t\tDRVNAME, &ps2_rtc_ops, THIS_MODULE);'
    if old_call in text:
        new_call = ('rtc = devm_rtc_allocate_device(&pdev->dev);\n'
                    '\tif (IS_ERR(rtc))\n\t\treturn PTR_ERR(rtc);\n'
                    '\trtc->ops = &ps2_rtc_ops;\n'
                    '\tclear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->features);\n'
                    '\treturn devm_rtc_register_device(rtc);')
        # Replace the complete obsolete block below, retaining the final return only once.
        start = text.index(old_call)
        end = text.index("\n}\n", start)
        updated[path] = text[:start] + new_call + text[end:]
    else:
        raise RuntimeError("Missing obsolete RTC registration block; inspect modern source before adapting")
    path = "drivers/ps2/gs-irq.c"
    text = read(path)
    start = text.index("static struct irqaction cascade_gs_irqaction = {")
    end = text.index("\n};\n", start) + 4
    text = text[:start] + text[end:]
    updated[path] = text.replace('setup_irq(IRQ_INTC_GS, &cascade_gs_irqaction)',
                                'request_irq(IRQ_INTC_GS, gs_cascade, IRQF_NO_THREAD, "GS cascade", NULL)', 1)
    path = "drivers/ps2/gs-registers.c"
    text = read(path)
    # GS is memory mapped, not an IO-port peripheral. Modern MIPS raw qword
    # helpers retain atomic 64-bit access on a 32-bit CPU with 64-bit registers.
    helpers = '''static inline u64 ps2_gs_readq(unsigned long addr)
{
\tmb();
\treturn __raw_readq((void __iomem *)CKSEG1ADDR(addr));
}

static inline void ps2_gs_writeq(u64 value, unsigned long addr)
{
\t__raw_writeq(value, (void __iomem *)CKSEG1ADDR(addr));
\tmb();
}

'''
    anchor = "/* Shadow write-only privileged Graphics Synthesizer registers. */"
    updated[path] = text.replace(anchor, helpers + anchor, 1).replace("inq(", "ps2_gs_readq(")
    updated[path] = updated[path].replace("outq(", "ps2_gs_writeq(")
    path = "arch/mips/include/asm/mach-ps2/gif.h"
    if "bool gif_busy(void);" not in read(path):
        add(path, "bool gif_busy(void);\n", "bool gif_wait(void);")
    path = "drivers/ps2/gs-sysfs.c"
    text = read(path)
    # Modern generic COUNT_ARGS accepts fifteen arguments, while this existing
    # GS field generator has up to twenty-five. Keep a private fixed bound.
    parameters = ", ".join(f"_{i}" for i in range(26))
    count = ", ".join(str(i) for i in range(25, -1, -1))
    helpers = (f"#define __PS2_COUNT_ARGS({parameters}, N, ...) N\n"
               f"#define PS2_COUNT_ARGS(...) __PS2_COUNT_ARGS(_, ##__VA_ARGS__, {count})\n\n")
    text = text.replace("COUNT_ARGS(", "PS2_COUNT_ARGS(")
    updated[path] = text.replace("#define SYSFS_STATEMENT1(", helpers + "#define SYSFS_STATEMENT1(", 1)
    for path in ("drivers/ps2/iop-irq.c", "drivers/ps2/sif.c"):
        updated[path] = read(path).replace("in_irq()", "in_hardirq()")
    # Architecture cascades retain the original handlers, with forced-threading
    # disabled; modern request_irq is available after mm_core_init in start_kernel.
    for name, irq in (("dmac", "IRQ_C0_DMAC"), ("intc", "IRQ_C0_INTC")):
        path = f"arch/mips/ps2/{name}-irq.c"
        text = read(path)
        start = text.index(f"static struct irqaction cascade_{name}_irqaction = {{")
        end = text.index("\n};\n", start) + 4
        text = text[:start] + text[end:]
        old_call = f"setup_irq({irq}, &cascade_{name}_irqaction)"
        new_call = f'request_irq({irq}, {name}_cascade, IRQF_NO_THREAD, "{name.upper()} cascade", NULL)'
        updated[path] = text.replace(old_call, new_call, 1)
    path = "arch/mips/ps2/time.c"
    text = read(path)
    call = "setup_irq(IRQ_INTC_TIMER0, &timer0_irqaction);"
    replacement = ('if (request_irq(IRQ_INTC_TIMER0, timer0_irqaction.handler,\n'
                   '\t\t\ttimer0_irqaction.flags, timer0_irqaction.name,\n'
                   '\t\t\ttimer0_irqaction.dev_id))\n'
                   '\t\tpanic("PS2: unable to register timer0 interrupt\\n");')
    if call not in text:
        raise RuntimeError("Inspect PS2 clock event registration before adapting")
    updated[path] = text.replace(call, replacement, 1)
    path = "arch/mips/ps2/memory.c"
    text = read(path).replace("#include <linux/mm.h>\n",
                              "#include <linux/mm.h>\n#include <linux/memblock.h>\n")
    text = text.replace("add_memory_region(0x00000000, 0x02000000, BOOT_MEM_RAM);",
                        "memblock_add(0x00000000, 0x02000000);")
    for name in ("ROM0", "ROM1"):
        text = text.replace(f"add_memory_region({name}_BASE, {name}_SIZE, BOOT_MEM_ROM_DATA);",
                            f"memblock_reserve({name}_BASE, {name}_SIZE);")
    updated[path] = text
    path = "arch/mips/ps2/reboot.c"
    updated[path] = read(path).replace("cpu_relax_forever();", "for (;;)\n\t\tcpu_relax();")
    path = "arch/mips/ps2/scmd.c"
    updated[path] = read(path).replace("rtc_time_to_tm(t + UTC_TO_JST, &tm);",
                                     "rtc_time64_to_tm(t + UTC_TO_JST, &tm);")
    path = "arch/mips/include/asm/stackframe.h"
    text = read(path)
    old_frame = (VENDOR / path).read_text()
    start = old_frame.index("#ifdef CONFIG_CPU_R5900\n\t\t/*\n\t\t * Reset bits 127..64")
    end = old_frame.index("\n#endif\n", start) + len("\n#endif\n")
    # Restore the exact existing fork macro: pcpyld preserves each register's
    # low 64 bits and zeroes its upper 64 bits before userspace return. This
    # retains the fork's documented o32 limitation; full MMI context is pending.
    anchor = "#if defined(CONFIG_CPU_R3000)\n\n\t\t.macro\tRESTORE_SOME"
    if "\t.macro\tRESET_MMR" in text or anchor not in text:
        raise RuntimeError("Inspect modern exception-return layout before restoring R5900 MMR macro")
    updated[path] = text.replace(anchor, old_frame[start:end] + "\n" + anchor, 1)
    path = "arch/mips/include/asm/cacheops.h"
    text = read(path)
    # The old Fill macro was renamed Fill_I, so its rejected hunk also left
    # the preceding R5900 cache-op conditional unclosed. Restore the original
    # R5900 operation values using the modern name and keep generic ops intact.
    boundary = "#define Hit_Writeback_Inv_D\t\t(Cache_D | Hit_Writeback_Inv)\n"
    if boundary not in text:
        raise RuntimeError("Inspect the modern cache operation constants before adapting")
    text = text.replace(boundary, boundary + "#endif\n", 1)
    start = text.index("#define Fill_I\t")
    end = text.index("\n", text.index("#define Hit_Writeback_D\t", start)) + 1
    generic = text[start:end]
    text = text[:start] + ("#ifdef CONFIG_CPU_R5900\n"
                           "#define Fill_I\t\t\t\t0x0e\n"
                           "#define Hit_Writeback_D\t\t\t0x1c\n"
                           "#else\n" + generic + "#endif\n") + text[end:]
    updated[path] = text

    path = "arch/mips/include/asm/r4kcache.h"
    pristine = ROOT / "kernel/vendor/linux-7.2.8/snapshot" / path
    updated[path] = modernize_r4kcache(read(path), (VENDOR / path).read_text(), pristine.read_text())

    # Processor-family module ABI identifiers moved out of asm/module.h.
    path = "arch/mips/include/asm/vermagic.h"
    if "CONFIG_CPU_R5900" not in read(path):
        add(path, '#elif defined CONFIG_CPU_R5900\n#define MODULE_PROC_FAMILY "R5900 "\n',
            "#elif defined CONFIG_CPU_NEVADA\n")

    # The short-loop erratum macro was imported into asmmacro.h, but its
    # memset include hunk conflicted with the moved linux/export.h include.
    # Keep the modern export header and restore the original macro dependency.
    path = "arch/mips/lib/memset.S"
    if "#include <asm/asmmacro.h>" not in read(path):
        add(path, "#include <asm/asmmacro.h>\n", "#include <asm/asm-offsets.h>\n")

    # The fork's cpu_relax_forever helper was not upstreamed. On R5900,
    # cpu_relax() is exactly barrier(); retain its fail-stop error behavior
    # without importing unrelated processor.h dependencies into the inflater.
    path = "arch/mips/boot/compressed/decompress.c"
    text = read(path)
    if "cpu_relax_forever();" not in text:
        raise RuntimeError("Inspect the decompressor error loop before adapting")
    updated[path] = text.replace("cpu_relax_forever();\t/* Halt */",
                                "for (;;)\n\t\tbarrier();\t/* Halt */", 1)

    # ELF loaders need not provide a usable EE Status register. In particular,
    # ordinary MIPS IE/mask setup cannot enable the R5900's separate EIE bit.
    # Restore the fork's entry contract before any decompressor memory access;
    # retain modern BSS clearing, argument preservation and jalr sequences.
    path = "arch/mips/boot/compressed/head.S"
    text = read(path)
    anchor = "\tmove\ts3, a3\n\n\t/* Clear BSS */"
    if text.count(anchor) != 1 or "0x30010000" in text:
        raise RuntimeError("Inspect the PS2 decompressor entry before normalizing CPU Status")
    replacement = ("\tmove\ts3, a3\n\n"
                   "#ifdef CONFIG_CPU_R5900\n"
                   "\t/* Kernel mode, IE off, EE EIE on; loaders may start in user mode. */\n"
                   "\tli\tk0, 0x30010000\n"
                   "\tmtc0\tk0, $12\n"
                   "\tsync.p\n"
                   "#endif\n\n\t/* Clear BSS */")
    updated[path] = text.replace(anchor, replacement, 1)

    # Relay firmware requests and SIF RPCs sleep and need incoming DMA IRQs.
    # The resource callbacks run outside the IRQ descriptor's raw spinlock.
    path = "drivers/ps2/iop-irq.c"
    updated[path] = modernize_iop_irq(read(path))

    # A queued SIF relay can arrive after OHCI status was acknowledged. The
    # generic IRQ_NONE path must not leave the PS2 workaround's MIE mask set.
    path = "drivers/usb/host/ohci-ps2.c"
    updated[path] = modernize_ohci_irq(read(path))

    # GNU MIPS TLS uses RDHWR even on pre-r2 CPUs. On R5900 its SQ encoding
    # traps as a user access to a kernel segment; retain the original PS2
    # address-error emulation while preserving modern user-access handling.
    path = "arch/mips/kernel/unaligned.c"
    updated[path] = modernize_unaligned(read(path), (VENDOR / path).read_text())
    path = "arch/mips/include/asm/traps.h"
    updated[path] = modernize_traps_header(read(path), (VENDOR / path).read_text())

    patch = []
    for path, after in updated.items():
        before = original[path]
        if before == after:
            continue
        patch.extend(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
                                        fromfile="a/" + path, tofile="b/" + path))
    patch_path = Path(args.patch_output).resolve()
    patch_path.parent.mkdir(parents=True, exist_ok=True)
    patch_path.write_bytes("".join(patch).encode())
    subprocess.run(["git", "apply", "--check", str(patch_path)], cwd=source, check=True)
    subprocess.run(["git", "apply", str(patch_path)], cwd=source, check=True)
    report = {"target": "Linux 7.2.8", "updated_paths": [p for p in updated if updated[p] != original[p]],
              "patch": str(patch_path), "complete_port": False,
              "remaining": "R5900 exception/TLB and compressed-boot errata, full multimedia context, graphics buffers/uploads, missing native drivers and physical hardware acceptance"}
    (source / ".eelinux-modernize.json").write_text(json.dumps(report, indent=2) + "\n")
    # Native Ethernet is a separate new driver, never folded into or confused
    # with acceptance of the incomplete R5900 core API adaptation above.
    report["native_smap"] = install_native_smap(source, ROOT / "kernel/patches/ps2-smap.patch")
    report["r5900_cp0"] = install_r5900_cp0(source, ROOT / "kernel/patches/7.2.8-r5900-cp0.patch")
    report["intc"] = install_intc(source, ROOT / "kernel/patches/ps2-intc-mask.patch")
    report["usb_boot"] = install_usb_boot(source, ROOT / "kernel/patches/ps2-usb-builtin.patch")
    report["sif_usb"] = install_sif_usb(source, ROOT / "kernel/patches/ps2-sif-usb-order.patch")
    report["ps2_dma_fault"] = install_ps2_fault(source, ROOT / "kernel/patches/ps2-dma-fault.patch")
    install_iop_upload(source, ROOT / "kernel/patches/ps2-iop-upload.patch")
    report["iop_firmware_upload"] = {
        "method": "SIF DMA, aligned allocation, bounded chunks, zeroed tail",
        "stalled_RPC": "report once; report changed mailbox phases; retain DMA/client ownership",
        "physical_USB_root_tested": False,
    }
    report["iop_loader_patch"] = install_iop_patch(source, ROOT / "kernel/patches/ps2-iop-uncached-patch.patch")
    report["r5900_user_vm"] = install_user_vm(source, ROOT / "kernel/patches/ps2-user-vm.patch")
    report["GS_mode_allocation"] = install_video(source, ROOT / "kernel/patches/ps2-video-memory.patch")
    report["r5900_page_cache"] = install_page_cache(source, ROOT / "kernel/patches/ps2-page-cache.patch")
    (source / ".eelinux-modernize.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(f"kernel-modernize: {exc}", file=sys.stderr)
        sys.exit(1)
