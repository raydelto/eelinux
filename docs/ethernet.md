# Ethernet implementation and evidence

eeLinux implements a native Linux SMAP driver in
[`kernel/new-drivers/ps2-smap`](../kernel/new-drivers/ps2-smap), paired with the
DEV9, interrupt-controller and IRQ-relay firmware in
[`kernel/iop-overlay`](../kernel/iop-overlay). The default kernel selects
`CONFIG_PS2_SMAP=y`, `CONFIG_PS2_IOP_BRIDGE=y` and `CONFIG_PHYLIB=y`. SMAP is built
into the kernel; USB Ethernet and every other Ethernet vendor family are
disabled. USB storage, keyboard and mouse support remain available.

The Linux driver and all six IOP firmware modules have been cross-compiled.
Native tests execute the actual packet, IRQ and DEV9 control functions against
independent MMIO and firmware-peer models under AddressSanitizer and
UndefinedBehaviorSanitizer. Packet delivery on a physical PlayStation 2 has not
yet been verified. The R5900 kernel source acceptance gate remains closed.

## Current physical result and cold-start candidate

The SCPH-90001 boots GNU userland on relay-v8, but SMAP probe returns
MIPS `ETIMEDOUT` (-145), and only loopback registers. This is before DHCP;
the old generic probe failure does not identify the timed-out operation.

Relay-v9 removes the requirement for an already-completed MDIO operation
before the first command. The driver tracks its own transaction, retaining
ownership after timeout until completion or a successful MAC reset. FIFO
reset now precedes EMAC reset, matching the SDK and original Linux sequence.
Each reset and probe failure reports its stage. Actual-C tests exercise a
zeroed station-management register, timeout/no-overwrite, late completion,
read errors and reset failures. The compiled source matches the tested code.

The new kernel passes PCSX2 DHCP, rebind, DNS and ICMP with zero RX/TX errors.
This is a tested cold-start correction and diagnostic candidate; it is not
yet a confirmed fix for the photographed physical timeout. The working v8
boot/cache fixes and IOP firmware are preserved. See
[physical observations](hardware-network-v8.json) and
[startup validation](native-smap-startup-validation.json).

On the full v9 image, run `eelinux-network diagnose`; with an ELF-only
update, run `dmesg -l err`. Neither command requires typing a pipe/backslash.

## Hardware coverage

| Console and Ethernet hardware | Implementation | Validation still required |
| --- | --- | --- |
| Slim with integrated Ethernet, including SCPH-90001 | DEV9 expansion-interface detection, power/reset and native SMAP MAC/PHY | Physical link, packets and sustained traffic |
| Fat with a Sony expansion-bay Network Adaptor | DEV9 expansion-interface path; shared ATA interrupts preserved | Physical networking and simultaneous ATA/network traffic |
| Early Japanese PS2 with appropriate PCMCIA Ethernet hardware | DEV9 PCMCIA revision detection, voltage/reset and card configuration | Physical card acceptance and hotplug |
| Missing Ethernet hardware, unsupported card or invalid EEPROM MAC | Probe returns an error; no synthetic interface | No Ethernet operation claimed |

AIF development-unit handling and PCMCIA hotplug are not qualified. The driver
cannot add a physical Ethernet port to a console that lacks the required hardware.

## Imported primary references

`kernel/vendor/ethernet/manifest.json` records immutable commits, original paths,
and SHA-256 for each imported file:

- The actual GPL Linux SMAP driver and header from
  [frno7's PS2 Linux 2.6.35.14 tag](https://github.com/frno7/linux/tree/ps2-v2.6.35.14/drivers/ps2).
- The GPL `smap-linux` **IOP** implementation from
  [ps2dev/ps2eth](https://github.com/ps2dev/ps2eth/tree/master/smap-linux).
  Its name does not mean it is a modern Linux `net_device` driver: it includes
  `loadcore`, `intrman`, `thbase`, `dev9`, and `ps2ip` IOP SDK interfaces.
- [SMAP](https://github.com/ps2dev/ps2sdk/blob/master/common/include/smapregs.h),
  [DEV9](https://github.com/ps2dev/ps2sdk/blob/master/common/include/dev9regs.h), and
  [SPEED](https://github.com/ps2dev/ps2sdk/blob/master/common/include/speedregs.h)
  register definitions and the SDK license. The SMAP EMAC is related to the IBM
  PowerPC 405 EMAC; sharing a MAC design does not make its bus/DMA driver portable.

These register-level primary sources satisfy the hardware reference search;
no board schematic is bundled or claimed to have been verified. They preserve
their original licenses and are reference inputs, outside the active Kbuild.
Regenerate with `python3 scripts/import_ethernet.py`; subsequent runs verify the
committed file hashes before replacing anything.

## Packet and interrupt implementation

The EE accesses SMAP registers at `0x14000000`; packet data moves through the
actual device FIFOs using programmed I/O. There are 64 hardware descriptors;
TX uses at most 63 and accounts for the 4 KiB TX FIFO. FIFO writes round to four
bytes with cleared padding. NAPI validates RX lengths, status and FIFO pointers
before copying packets. TX completion accounts for retired descriptors; a full
queue returns `NETDEV_TX_BUSY` without consuming the caller's packet.

No shared EE/IOP DMA ring or cached EE packet buffer is handed to DEV9 DMA.
Descriptor ownership still requires explicit write barriers and MMIO readback.
DMA acceleration is a separate change requiring cache and arbitration validation.

The IOP owns SPEED's shared Ethernet/ATA interrupt mask. It masks the SMAP group,
acknowledges Ethernet through the correct `SPEED + 0x128` interrupt-clear register,
and relays IRQ identity to the EE. The driver schedules NAPI from that identity:
the status was already acknowledged. It never modifies the shared mask directly.
NAPI rearm uses a SIF command, with bounded retry work for a busy command channel.

PHY discovery and negotiation use Linux MDIO/PHY support. MDIO transactions and
resets have bounded waits. The driver implements multicast filtering, validated
MAC changes, link settings, statistics, close/reopen and TX timeout recovery.
The actual factory EEPROM MAC must pass checksum and Ethernet-address checks.

## Firmware transport and built-in ordering

DEV9 control uses command `0x8000001c`, within the IOP ROM's 32-entry command
table. Acquire/release requests carry an operation and lease cookie, not a pointer.
Replies include the cookie, operation tag, status, hardware revision and MAC.
Release preserves the acquired cookie; late replies for another operation or
lease are ignored. This new exchange waits at most ten seconds. IRQ rearm uses
command `0x8000001b` and a mask restricted to SMAP IRQ bits `0x007c`.

SIF, heap, IOP module service and IRQ routing are ordered built-in device
initcalls. Readiness checks reject dependent requests after earlier initialization
failure. SMAP probes as a late initcall using firmware in the embedded initramfs.
The adapted `dev9.irx` is module version `0x0101`; its ATA export-library version
remains `0x0100`.

Inherited generic IOP firmware-loader and IRQ-resource RPC waits remain
synchronous. The latest USB candidate reports a stalled bind/RPC once and only
subsequent changes in the relay's shared startup mailbox, while retaining
DMA/client memory ownership. IRX uploads now use aligned SIF DMA instead of
cached EE stores into the independently managed IOP heap. Relay startup avoids
blocking IOP logging and uses a 4 KiB RPC stack. These
waits remain separate from the bounded new DEV9 control exchange and are an
open recovery limitation.

The factory EEPROM reader now consumes the Microwire dummy zero bit before
the 64 data bits. Tests cover the resulting word/MAC alignment, bad dummy bits
and restoration of shared PIO state on errors. The Windows guest reads MAC
`00:04:1f:82:b6:9c`, acquires DHCP automatically, rebinds, resolves `gnu.org`,
and receives external ICMP replies with zero RX/TX errors. This is emulator
evidence, not a physical-network acceptance result. See
[runtime evidence](network-runtime-validation.json).

PCSX2's virtual DHCP server handles option 119 as a DNS search list and ignores
the unsupported FORCERENEW capability in option 145. Its SMAP model also accepts
the valid 16-bit RX FIFO read pointer. Malformed options and unsupported register
accesses still report errors. The relevant contracts are
[RFC 3397](https://www.rfc-editor.org/rfc/rfc3397.html),
[RFC 6704](https://www.rfc-editor.org/rfc/rfc6704.html), and the
[SDK SMAP definitions](https://github.com/ps2dev/ps2sdk/blob/master/common/include/smapregs.h).

## Build evidence and hardware acceptance

[`scripts/kernel_smap_port.py`](../scripts/kernel_smap_port.py) installs the
driver and ordered bridge through checked source anchors, producing a separate
[`kernel/patches/ps2-smap.patch`](../kernel/patches/ps2-smap.patch). The existing
kernel API patch and incomplete R5900 acceptance status are preserved.

Run the actual-handler host tests on Linux:

```sh
python3 -m unittest tests.test_ps2_smap -v
```

Tests cover packet/FIFO wraparound and alignment, saturation, malformed RX,
allocation failure, IRQ rearm/retry exhaustion, malformed and late replies,
timeout cleanup, and failed built-in bridge initialization. Windows runs the
portable guards and skips native C tests. The separate IOP firmware provenance
and tests are recorded in
[`docs/iop-dev9-validation.json`](iop-dev9-validation.json).

On actual supported hardware, acceptance still requires DHCP acquisition/renewal,
static IPv4, DNS/ARP, bidirectional transfer checksums, packet loss, cable
reinsertion, close/reopen and simultaneous disk/network activity. Emulator
results alone cannot establish physical PHY or packet correctness.

## Earlier USB Ethernet baseline

[frno7's networking guide](https://github.com/frno7/linux/wiki/Networking-for-the-PlayStation-2)
documents AX88179 USB Ethernet as its current working option; it explicitly
states that native PS2 Ethernet is not implemented in that upstream project.
eeLinux's native driver is a separate implementation. Earlier eeLinux audit
builds selected upstream USB Ethernet; current native SMAP builds disable it
at the user's request. DHCP/static configuration with lease renewal remains.

Check `dmesg` for `native SMAP`, its factory MAC and discovered PHY. Configure
the actual interface in `/etc/network.conf`, inspect `/sys/class/net`, then run
`eelinux-network restart` and `eelinux-network status`. Do not assume an interface
name or a USB adapter chipset is correct without checking the enumerated device.
