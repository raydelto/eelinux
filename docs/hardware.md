# eeLinux hardware and kernel implementation

The PS2 has 32 MiB of EE memory, and the kernel uses a little-endian R5900 o32
ABI. This project keeps a small early initramfs containing `/init`, required
kernel modules and IOP firmware. GNU utilities, libraries and Xorg/TWM belong on
disk. Production PAL USB images use ext4 on `/dev/sda2`. Standalone DVDs use
`/dev/ps2dvd`, ISO9660, a read-only ext4 loop and a bounded RAM overlay.
Use `--root-device` to change a disk root and rebuild the ELF. Production media
selects 720x576 progressive at 50 Hz explicitly.

Two source profiles are explicit. `7.2.8` is the requested upstream release, now
with all added frno7 PS2 driver files imported and a checked partial forward
port. The architecture still needs work before a linked boot image is valid.
`baseline` is frno7's existing PS2 port of Linux 5.4.221. It has been cross-built
here as a kernel and modules, and remains untested by this project on physical
hardware. Original commits, full deltas, experimental controller/ATA branches
and licenses are preserved in `kernel/vendor/`.

| Hardware | Imported implementation | Current practical limit |
| --- | --- | --- |
| EE R5900, MMU/cache, IRQs, DMA, timer | PS2 platform, core MIPS delta, headers | Modern architecture adaptation remains; baseline cross-build passes |
| SIF/IOP RPC, heap, registers, linker, IRQs | All `drivers/ps2` code, six IOP modules | Early Linux modules and IRX must load before OHCI/storage |
| USB OHCI, storage and HID | PS2 host controller and standard Linux drivers | No physical eeLinux storage/input tests yet |
| USB Ethernet | AX88179/178A, AX8817X, CDC, DM9601, RTL8152 | AX88179 tested upstream; shared USB 1.1 bandwidth |
| Sony native/slim Ethernet | Native driver work tracked separately | Missing from frno7 main; SMAP/PHY implementation needs hardware acceptance |
| DEV9/ATA | PS2 bridge, PATA; experimental ATA branch | Appropriate physical adapter required; data-integrity tests pending |
| GS text console | GS, GIF, ps2fb | PAL/NTSC autodetection; no userspace framebuffer mmap |
| GS DRM | All upstream experimental source | Buffer allocation and uploads incomplete; DRM_GS disabled |
| UART/RTC | PS2 UART and RTC | External serial wiring; physical tests pending |
| DualShock controller | Main/feature controller and IOP source | Feature branch APIs need careful integration and tests |
| CD/DVD | Native read-only DVD-video IOP service and Linux optical block driver | Standalone Linux DVD root; physical drive and FreeDVDBoot firmware acceptance pending; no CD/audio/write support |
| SPU2, memory cards, iLink, VU acceleration | No complete main-branch Linux implementations | Port, license-check and test before advertising support |

`kernel/drivers.json` is the machine-readable inventory. Generic “PS/2” keyboard
controller options refer to PC keyboard ports and do not provide console support.

## Toolchains and boot sequence

The kernel compiler must emit real R5900 MIPS code. The normal kernel prefix is
`mipsr5900el-unknown-linux-gnu-`. A separately configured Linux compiler/libc is
necessary for GNU userland; the PS2DEV bare-metal libc cannot supply Linux
syscalls. Full kernel probes here used Debian `mipsel-linux-gnu-` GCC 14.2 with
the kernel's `-march=r5900` settings; the modern cache object additionally passed
with the project GCC 14.4/binutils 2.44 Linux compiler. Official PS2DEV IOP
`mipsel-none-elf-` GCC 15.2/binutils
2.45.1 builds all six IRX objects. Target-only freestanding headers and a generated
`elf32-littlemips` linker-format adaptation retain upstream firmware logic.

`kernel.py build` verifies a static MIPS ELF `/init`, checks post-Kconfig PS2
settings, builds kernel/modules, installs disk modules, computes transitive early
module dependencies with host `depmod`, checks all six IRX headers and hashes
against their built manifest, copies the early subset/IRX and manifest into
initramfs, then rebuilds the compressed ELF. `/etc/eelinux-modules` lists absolute
module paths in load order for the tiny early `/init`. No userspace module loader
is required before disk root exists. Select `--boot-storage ata` to include early
DEV9/PATA modules. All built modules are also installed on disk.

The output is a compressed **ELF**, not a PC bzImage or a PS2SDK application.
Load the generated `.elf` through wLaunchELF/uLaunchELF. The upstream platform
loads compressed code at physical 8 MiB and decompresses the kernel starting
at 64 KiB. The build rejects an ELF or unpacked early newc archive above 4 MiB as a
conservative budget. It also checks actual ELF load ranges against 32 MiB RAM
and rejects overlap between the decompressed kernel and compressed code/heap/
stack. Runtime memory measurements remain required. Initial display can take
several seconds. Early `/init` mounts device,
proc and sys filesystems, loads modules, mounts disk root and hands control to
GNU SysVinit.

## Repairs and actual checks

`driver-correctness.patch` fixes IOP root-device error-pointer checking, OHCI's
NULL ioremap check, framebuffer teardown lifetime/null handling, UART ownership
and an invalid global-pointer free, and GS DRM's false allocation success and
wrong remove-data type. DMA package pages are retained when GIF drain times out:
freeing a page still owned by DMAC permits access to reused RAM. This deliberately
leaks one page per failed teardown and logs the fault until a hardware-proven
stop/reset path exists. Incomplete GS DRM remains disabled.

Baseline configuration including TTY/VT, UART console, PTYs and HID, and a complete
kernel/modules cross-build passed with the fixes (42 modules, 4,178,884-byte
uncompressed audit kernel). Real 7.2.8 compile probes passed for all
`drivers/ps2/` C objects, PS2 OHCI, RTC, UART console, ps2fb and PATA after API
adaptation. All PS2 platform C files and cache assembly compile, and several core
exception/syscall/context-switch assembly files assemble. The focused cache
adapter preserves modern EVA/node helpers and protected exception fixups while
restoring the fork's R5900 synchronization and index-address semantics. Generated
assembly was reviewed with both audit and project compilers.

A full 7.2.8 audit build now links `vmlinux` (5,547,776 bytes), all 43 configured
modules and compressed `vmlinuz` (1,915,656 bytes). It uses a placeholder early
`/init`; the compressed CP0/cache entry sequence and R5900 exception/TLB errata
remain unreviewed. Full multimedia context is also incomplete. Sixty-four files
retain rejected hunks and the release-image gate stays closed.
All six IRX builds passed with official PS2DEV. These are build
checks: boot, Ethernet transfers, storage integrity and a usable Xorg display
remain physical hardware acceptance tasks. `kernel/validation.json` records the
compiler, pinned versions, successful checks and unresolved requirements.

Primary references: [kernel.org releases/checksums](https://www.kernel.org/pub/linux/kernel/v7.x/),
[frno7 PS2 kernel](https://github.com/frno7/linux),
[kernel build](https://github.com/frno7/linux/wiki/Building-a-PlayStation-2-Linux-kernel),
[ELF boot](https://github.com/frno7/linux/wiki/Installing-and-booting-PlayStation-2-Linux),
[networking](https://github.com/frno7/linux/wiki/Networking-for-the-PlayStation-2),
[hardware models](https://github.com/frno7/linux/wiki/PlayStation-2-Linux-hardware-model-compatibility),
and [IOP firmware](https://github.com/frno7/iopmod).
