# DEV9 firmware overlay

This GPL-2.0 overlay adapts the pinned Fredrik Noring IOP modules at
`12cdf14b78ed5e648ffe1d52e6754f00b3b69001`. The original import and its hash
manifest remain unchanged. `scripts/iop_dev9.py` verifies the import, creates
an isolated source tree, records the source transformations, and builds the
same six real IRX modules with the ps2dev R3000 compiler.

The builder retains upstream `-O0`. An isolated same-source comparison found
that `-O2` printk, DEV9 and IRQ modules linked but returned `MODULE_EXIT`;
the six `-O0` modules returned `MODULE_RESIDENT` and reached native `eth0`
registration, EEPROM MAC acquisition, ext4 root mounting and GNU/PAM login
in the patched emulator. The optimized startup/relocation cause remains
unresolved, so optimization is not a qualified production setting. The
portable proof is [iop-dev9-validation.json](../../docs/iop-dev9-validation.json).

DEV9 register access now reads the power register value, serializes users
with a process-context semaphore, and initializes the 0x30 expansion/Slim
bridge or the 0x20 PCMCIA bridge. The PCMCIA path checks voltage, card type,
ready status, bounded CIS tuples and the network card manufacturer/product
identity before changing the bus mode. EEPROM transactions preserve the PIO
pins and direction, cache the factory MAC before ATA can use SPEED, and reject
bad checksums, zero MACs and multicast MACs. Closing Ethernet releases its
reference; it does not power-cycle the shared ATA bus.

The READ transaction now samples the initial zero dummy bit separately before
clocking the four data words. This matches the SDK transaction and the
[M93C46 datasheet](https://www.st.com/resource/en/datasheet/m93c46-w.pdf), section
5.1. The former sequence gave a shifted MAC in PCSX2. Both successful reads and
invalid dummy bits are exercised by the actual-C MMIO tests; every exit restores
the shared PIO pins and direction.

The hardware register and initialization sequence references are
[pinned ps2sdk DEV9 source](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/iop/dev9/dev9/src/ps2dev9.c),
the preserved GPL driver in `kernel/vendor/ethernet/legacy-linux/smap.c`, and
[pinned SPEED register definitions](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/common/include/speedregs.h).
The SDK is an AFL-2.0 hardware reference; its module and interrupt controller
are not linked or copied into this GPL firmware. The replacement uses the
existing GPL IOP interfaces and its own state and control transport.

The current DEV9 module is version 1.2. Its separate packet server at
`0x800001d0` registers before module entry returns, then performs hardware
initialization when Linux requests it. It uses IOP addresses for FIFO, MAC
and MDIO access, which avoids dependence on the legacy EE DEV9 window.
The [kernelloader documentation](https://raw.githubusercontent.com/rickgaiser/kernelloader/master/readme.txt)
also requires an IOP SMAP RPC driver for Slim Ethernet; this is the basis for
using the IOP backend on all supported models. Physical compatibility of this
new implementation is now user-confirmed for basic connectivity on the
SCPH-90001: dynamic IPv4, four external ICMP replies with no loss and
successful `ping gnu.org`. The user reports no messages from `dmesg -l err`.
[Physical evidence](../../docs/hardware-network-validation.json) records the
report and the unmeasured console kernel hash. Other models and
sustained traffic remain to be qualified.

Linux queues at most 32 SKBs and calls the service from process context. Each
1616-byte request/reply has a version and lease cookie; malformed lengths and
ownership errors are rejected. FIFO/MAC/MDIO waits are bounded. Packet RPCs
have a two-second timeout; a transport timeout quarantines the client and DMA
pages until reboot so a late completion cannot overwrite freed or reused data.
The polling backend takes no shared SPEED interrupt mask or DEV9 DMA channel.
Actual-C tests exercise independent IOP MMIO, packet wrap/padding, malformed
descriptors, PHY scanning, timeouts and reset/lease recovery with sanitizers.

[ps2-smap-firmware.patch](../patches/ps2-smap-firmware.patch) exports the matching
firmware source; [current validation](../../docs/ps2-smap-validation.json)
records the compiled kernel/firmware and emulator packet checks.

## Legacy direct EE backend

The earlier control transport uses system command IDs below the ROM's 32-slot limit.
[SIFCMD dispatch](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/iop/system/sifcmd/src/sifcmd.c)
clears `packet_size` before the IOP callback, so the command's `opt` identifies
the fixed payload. EE callbacks additionally check the actual packet size.

| Command | Direction | Payload | `opt` |
| --- | --- | --- | --- |
| `0x8000001c` | EE to IOP | `u32 operation, u32 cookie` | `0x44560908` |
| `0x8000001c` | IOP to EE | `u32 cookie, s32 status, u32 hardware, u8 mac[6], u16 checksum` | `0x44560014 \| (operation << 8)` |
| `0x8000001b` | EE to IOP | `u32 SMAP interrupt mask` | `0x534d0104` |

All fields are little endian. Operation 1 acquires the single EE Ethernet
lease, operation 2 releases the same nonzero cookie. Duplicate acquisition is
idempotent; another cookie receives `-EBUSY`. Replies distinguish acquisition
and release. A bounded eight-entry worker queue reserves room for cleanup;
overflow release requests have a separate cleanup slot. Invalid or overloaded
requests cannot acquire a hardware reference. The EE driver times out after
10 seconds, queues release, deregisters its callback and synchronizes SIF0
before freeing memory. No pointers are sent across the interface.

The worker retries reply submission for at most one second and completion for
at most one second. If DMA completion times out, its static packet remains
untouched and future replies are disabled; queued lease cleanup still runs.
The existing IOP module loader and IRQ resource RPC remain synchronous with
inherited unbounded waits. This overlay does not claim to fix that broader
firmware infrastructure.

Only the IOP owns `SPD_INTR_MASK`. A SMAP interrupt masks the complete `0x7c`
group before relay and acknowledges its cause at `SPEED+0x128`, not the
read-only SPEED status register. NAPI completion sends rearm; only live IRQ
mappings and allocated descriptors can be enabled. ATA causes retain their
own device acknowledgement. The outer expansion or PCMCIA bridge latch is
acknowledged separately. Out-of-range/unallocated pending bits are masked
before indexing the descriptor array.

The cross-build and host tests establish source/ABI integrity and exercise
state and MMIO behavior. They do not establish hardware packet delivery.
The SCPH-90001 has a user-confirmed basic Ethernet success report with the
IOP backend. Other Slim revisions, official Fat Network Adaptor and early
PCMCIA cards still need real console acceptance. Card hot removal/reinsertion and AIF development units
need additional handling; neither is claimed by this implementation.
