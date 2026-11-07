# Linux 7.2.8 PS2 port

The actual kernel.org 7.2.8 release is SHA-256 pinned in `kernel/sources.json`.
The unmodified upstream release has no PS2 platform. eeLinux imports all added
source files from frno7's PS2 fork and attempts every modified-file compatibility
hunk. Source import and hardware completion are different stages.

Run on Linux, or inside WSL on its native filesystem:

```sh
python3 scripts/kernel.py fetch --profile 7.2.8 --source "$WORK/sources/linux-7.2.8"
python3 scripts/kernel.py port --source "$WORK/sources/linux-7.2.8" --report "$WORK/port-report.json"
# The partial transplant returns 2 and preserves .rej files; inspect its report.
python3 scripts/kernel_modernize.py --source "$WORK/sources/linux-7.2.8" \
  --patch-output "$WORK/modern-api.patch"
python3 scripts/kernel.py audit --profile 7.2.8 --source "$WORK/sources/linux-7.2.8"
```

The importer verifies every snapshot hash, copies complete added files, applies
the modified-file patch with rejects retained, then applies the driver
correctness fixes. The modern API adapter emits and checks a reviewable patch
for registration, platform removal, RTC, GS qword MMIO, cascaded IRQ, CPU
numbering, UART FIFO/timer APIs, framebuffer bitmap APIs, bounded GS sysfs macro
generation, memblock memory registration, module ABI family identification,
the memset short-loop macro include, the decompressor fail-stop loop and the
R5900 compressed-entry CPU Status normalization (including EE EIE). It restores
the exact upstream `RESET_MMR` macro: upper 64 multimedia-register bits are
cleared before o32 userspace return; full 128-bit register context preservation
is still pending. Its focused cache adapter retains modern generic/EVA/node
helpers and exception fixups while restoring R5900 synchronization, the
instruction-index address erratum and exact upstream 64-byte unrolled sequences.
It is restricted to the
imported 7.2.8 tree, and refuses repeat adaptations. The checked reference
adapter output is `kernel/patches/7.2.8-api.patch`.

The first transplant produced 64 files with rejected hunks. Rejections are
retained even where the modern adapter covers corresponding registration;
they document work that must be reviewed. Actual compile probes now accept
the R5900/TTY/VT/HID configuration and build all `drivers/ps2/` C objects plus
PS2 OHCI USB, RTC, UART console, ps2fb and PATA objects. All `arch/mips/ps2/`
C objects and `cache.S` also compile. Core `head.S`, `entry.S`, `genex.S`,
`r4k_switch.S` and `scall32-o32.S` assemble. A complete selected configuration
now links `vmlinux` (5,547,776 bytes), all 43 modules, and compressed `vmlinuz`
(1,915,656 bytes) with Debian GCC 14.2. The cache object also compiles with the
project's GCC 14.4/binutils 2.44 compiler in a separate output tree. Reviewed
disassembly confirms 32 D-cache operations with a leading `sync.l` and one after
each operation, and 32 I-cache operations bracketed by `sync.p`; instruction
index addresses start at zero. `kernel/cache-review.json` records this evidence.
The checked adapter patch reproduces byte for byte. Actual ELF load ranges pass
the 32 MiB RAM and decompressor overlap checks; adding real early modules/init
changes these ranges and the release builder checks them again.

These are audit artifacts without production early-root/modules/firmware, and no compile
blocker remains for this selected configuration. A bootable release still needs
review of the short-loop BSS clear and
cache-flush handoff, exception-vector FLX05 padding, TLBR page alignment and
`sync.p`/NOP hazards, cache-error CP0 sequencing, and full multimedia context.
`kernel/validation.json` lists the exact rejected source paths. Native Ethernet
and a complete userspace boot have not been established. `configure` and `build` refuse release
images while rejects remain. The historical full-build and ELF-layout log paths
in the validation records refer to local generated evidence. Those logs are
preserved outside this source-only checkout after cleanup; new builds should
keep logs under their work directory. `kernel/modern-build.json` retains the
recorded generated module hashes.

The follow-up clean `kernel-console-7.2.8.elf` builds ps2fb in, keeps serial
output alongside tty0 and enables the modern fbdev device interface. PCSX2
2.6.3 now completes delay calibration and displays readable GS console text
at 576×460 PAL. It waits for `/dev/sda1` because its initramfs is empty and USB
root drivers remain modules. [Console evidence](../../../emulator/pcsx2-console-validation.json)
records the actual ELF/configuration/log hashes and visual observation.

Port and verify in this order:

1. R5900 CPU detection, o32 context switching, ISA/short-loop errata, FPU,
   exception entry, user access, cache/TLB maintenance and 32 MiB memory layout.
2. ELF decompressor entry and overlap limits, EE INTC/DMAC, clock and serial
   console; boot a small initramfs on actual hardware.
3. SIF DMA/RPC and IOP heap/linker/IRQ relay, then USB OHCI, storage and HID.
   The PS2DEV toolchain builds the actual six upstream IOP firmware modules.
4. GS console, RTC, DEV9 ATA and controller. Finish a shadow buffer and GS DMA
   upload path for Xorg. Existing ps2fb has no mmap, and imported GS DRM
   buffer-allocation/upload paths are unfinished and disabled.
5. Implement and validate native SMAP/EMAC3 networking, including PHY/MII, IOP
   ownership, packet rings, DMA/PIO, interrupt acknowledgement, NAPI and bounded
   reset/timeout handling. Test fat adapters and slim models independently.
6. Port audio, memory cards, CD/DVD and remaining peripherals from appropriately
   licensed implementations, then verify sustained I/O, teardown, shared IRQs
   and low-memory recovery on every supported model.

Use the explicitly selected, separately pinned `baseline` profile to build the
existing Linux 5.4.221 PS2 implementation while modern architecture work proceeds.
