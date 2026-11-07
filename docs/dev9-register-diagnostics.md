# PCSX2 DEV9 byte-read diagnostics

The three messages at `1000002c`, `10001000` and `10001030` are printed by
PCSX2, not by a Linux kernel error path.

| Address | Register read by the IOP firmware |
| --- | --- |
| `0x1000002c` | SPEED PIO direction, saved during EEPROM access |
| `0x10001000` | SMAP transmit FIFO control/reset |
| `0x10001030` | SMAP receive FIFO control/reset |

These are valid register addresses. The
[PS2SDK SPEED definitions](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/common/include/speedregs.h)
and [SMAP definitions](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/common/include/smapregs.h)
describe them. The current emulator lacks explicit read cases and falls back
to its stored register bytes while printing “Unknown”. See
[PCSX2 DEV9 reads](https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2/DEV9/DEV9.cpp)
and [SMAP reads/reset writes](https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2/DEV9/smap.cpp).

For the FIFO control reads, zero means the reset bit is clear; it is the
completion condition the firmware waits for. These diagnostics occurred in
the checked kernel run that registered `eth0`, acquired DHCP, completed
external ICMP and DNS, and reported zero RX/TX errors. They did not block
that run. They still identify a gap in explicit emulator register handling,
and are not evidence of physical Ethernet compatibility. See
[the matching run](ps2-smap-validation.json).
