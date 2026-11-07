# Earlier hardware boot investigation

Preserved relay-v2 through relay-v7 notes. Paths, current-candidate claims
and pending results below describe those earlier tests; use
[the current hardware guide](hardware-test.md) for relay-v8.

## Current hardware findings

On the user's SCPH-90001 with Tuna/LaunchELF and PS2-to-HDMI, the stopping
diagnostics have shown red at entry, yellow after BSS clearing, green after
decompression, cyan after cache handoff, and magenta at the main kernel entry.

These are user-reported checkpoint results. The `EESYNC.ELF` candidate still
stays blank; its three early R5900 synchronization guards did not resolve boot.

The `EEPRE.ELF` and `EEPOST.ELF` probes also stayed blank. Those are historical results:
the user has since confirmed Linux text and `/init` on real hardware after the R5900 CP0
correction.

The user's latest photo confirms that the relay-v5 kernel enumerates the
SanDisk USB stick and mounts ext4 `/dev/sda2`. The next failure is in the GNU
dynamic loader, with EPC at `ld.so.1+0xb614` and invalid read at `0x268`.

The matching binary decodes that instruction as `lw a1,616(s0)`, indicating a
null `s0`. The source-corruption cause is not yet proven. USB discovery is no
longer the stopping point in that photo.

Relay-v6 restores missing R5900 FLX05 guards in the TLB refill entry and
dedicated interrupt vector, retaining the 32-instruction refill limit. The
active non-EVA cache helpers already contain the required synchronization;
their binary audit is recorded separately. Relay-v7 also corrects framebuffer
allocation and VGA output and adds the resolution selector. The exact current
kernel boots GNU SysVinit in PCSX2 and runs the private vector/loader/network/
video checks. Physical dynamic-init/login acceptance remains pending. See
[userland investigation](hardware-userland-validation.json) and
[console resolutions](video-modes.md).

The current image is

Flash this whole image with Etcher, safely eject, then use Tuna/LaunchELF to run

`mass:/EELINUX.ELF`. Its first partition is FAT32 for LaunchELF; Linux root is

ext4 on `/dev/sda2`. It includes GNU tools without BusyBox, dhcpcd 10.5.2, UK
console keys and built-in native SMAP. USB core, OHCI, PS2 OHCI, SCSI disk,
USB storage, HID and IOP register support are built into this kernel. The early
kernel module manifest is empty: `/init` does not load those `.ko` files.

IOP IRX firmware still runs on the separate R3000 processor; it cannot be
linked into the Emotion Engine's Linux executable as a Linux driver.

The previous built-in candidate passed emulator USB root, UK keys, automatic
DHCP, DNS and external ping, but the user's physical PS2 stopped at

`IOP USB: loading IRQ relay`. The relay-v2 retest then produced an Oops followed

by `Attempted to kill init! exitcode=0x0000000b`. The instruction sequence is
consistent with the shared memory-copy routine at `0x803b8a74` in that build;
EPC, RA, BadVA and Cause were not available, so the caller is still unknown.

The current candidate retains the earlier SIF shared DMA packet
serialization and DMAC mask handling, preserving unmaskable DMA bus faults,
and prepares the relay before configuring
USB DMA or enabling its PS2 gate. It prints finite firmware startup checkpoints.

An unanswered RPC prints one warning and only later changes in startup phase.

Outstanding DMA/client
memory remains owned until a response arrives; it is not freed after a timeout.

Relay-v3 additionally prepares SIF/RPC receive pages before IOP DMA writes,
preventing dirty cache lines from a previous allocation from overwriting a
reply. This follows the CPU/device ownership rule described in the
[Linux DMA guide](https://docs.kernel.org/core-api/dma-api-howto.html).

The regression test reproduces stale data with the old RPC code and the correct
reply with the patched code. That is evidence for a DMA defect, not proof of the
physical panic's cause. Physical USB disk-root/login remains an open acceptance
test. Named kernel symbols add approximately 384 KiB to this test kernel's
uncompressed footprint; its checked load ranges still fit 32 MiB.

The user's relay-v3 photo shows LOADFILE RPC 6 waiting to start `irqrelay`,
with DMAC status `0x200044`. It also shows an earlier native SMAP probe timeout
(`-145`, MIPS `ETIMEDOUT`). This is a shared IOP/module-startup failure, before
USB disk discovery. Relay-v4 changes firmware upload from cached EE writes
into IOP heap memory to aligned SIF DMA, with zeroed tails and preserved
in-flight allocation ownership. Relay entry no longer waits for a debug-print
DMA; its RPC thread stack is 4 KiB. The startup mailbox is detached before
LOADFILE returns, so a later worker cannot write a freed allocation.

These changes address unsafe cache ownership and a blocking startup path;
neither is yet confirmed as the physical failure's precise cause. The exact
relay-v5 kernel passes PCSX2 USB/ext4 root, all 13 UK symbols, automatic DHCP,
rebind, DNS and two external ping replies with zero RX/TX errors. If hardware
still stops, the last `IOP relay: phase` line identifies entry, CreateThread,
RPC queue/register, StartThread, rearm, worker or resident state without
flooding the screen. See [IOP startup evidence](iop-startup-validation.json).

Relay-v5 also corrects a malformed IRX linker layout. Earlier relay files
ended initialized data on a two-byte boundary; the IOP loader copies and
clears words. Read-only data is now included in the text boundary, section
padding is preserved, and module text/data/BSS sizes cover the complete
load segment. All six firmware images are checked before packaging. This
addresses a concrete loader defect; physical acceptance still requires
a retest. See the [SDK loader](https://github.com/ps2dev/ps2sdk/blob/master/iop/system/loadcore/src/loadcore.c).

In a separate private test, relay entry deliberately waits before CreateThread.

The old malformed image reported phase 0; the rebuilt image reports phase 2
correctly. Across the 90-second test it prints exactly one stall warning and
one phase report. This injected-stall image is excluded from the release.

The upload and RPC threading references are the
[SDK SIF transport](https://github.com/ps2dev/ps2sdk/blob/master/iop/system/sifcmd/src/sifcmd.c)
and [RPC implementation](https://github.com/ps2dev/ps2sdk/blob/master/iop/system/sifcmd/src/sifrpc.c).

If the new kernel crashes, its last lines beginning `eeLinux fault` repeat
the task, EPC/RA, BadVA/Cause, a0/a1/a2, stack pointer and last initcall below
the long trace. Those details should stay on the final screen. The startup
banner identifies this build as `relay-v7 exception guards and video modes kernel`.

PID 1 user faults also print seven compact `eeLinux user fault` register lines
without dereferencing damaged user pointers.

The actual reporter has been exercised in PCSX2 using synthetic registers; its
values and named caller were verified. The physical exception path remains a
retest, and the test-only module is absent from the release image. See
[panic investigation evidence](hardware-panic-validation.json).

For a quick kernel-only retest on the existing FAT32/ext4 stick, replace its

`mass:/EELINUX.ELF`. Flash the whole image for matching optional disk modules.

The password for explicitly provisioned development images is `eelinux`, with
username `root`. Target PAM checks passed on the project-patched Linux and
Windows emulators after correcting filesystem permissions. The earlier original
image below predates that correction and keeps root locked.
