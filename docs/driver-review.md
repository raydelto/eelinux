# PS2 driver review and native Ethernet rewrite requirements

This review describes the pinned source files in this repository. The vendor
files remain reference material; they are not a claimed Linux 7.2.8 driver.
Line numbers refer to the original vendored files, before local patches.
The currently supplied driver corrections target the 5.4 PS2 baseline. They
must also be reviewed against the real newer kernel API before a port is enabled.

## Native SMAP dependencies

The legacy Linux driver is not an independent platform network driver that can
be compiled into a new tree by changing its Makefile. It relies on these
services and conventions:

| Dependency | Source evidence | Required work |
| --- | --- | --- |
| DEV9 detection and EE bus mapping | `legacy-linux/smap.c:2345` tests `ps2_pccard_present == 0x0100`; `smap_base_init` at 1855 uses the fixed bus base from `smap.h:143` | Model supported fat/slim/PC-card hardware, obtain legitimate register resources and coordinate ownership with ATA/DEV9 firmware. Do not copy the old platform flag into a new fake probe. |
| EEPROM MAC address | `smap_get_node_addr` at 1824 reads EEPROM words and checksum through DEV9 PIO pins | Preserve validated unicast MAC extraction and checksum rules; define an explicit fallback policy when EEPROM access fails. |
| Descriptor/FIFO layout | `smap.h:143` defines 4 KiB TX and 16 KiB RX FIFO areas; descriptor rings at 167 contain 64 entries each | Preserve FIFO wrapping, padding, descriptor ownership and frame-counter updates with proper I/O ordering. These are hardware rings, not a requirement to allocate 128 full host packet buffers. |
| Register widths and byte order | `smap.h:276` provides byte/word/dword I/O; `EMAC3REG_READ/WRITE` at 465 split the high and low 16-bit halves | Implement the same device byte order with kernel I/O accessors; a native `readl` alone is not an equivalent EMAC access. |
| Interrupt routing | `smap.c:2437` uses the legacy `IRQ_SBUS_PCIC`; interrupt processing begins at 848 | Establish the actual DEV9/IOP interrupt bridge and acknowledge only SMAP bits. Coordinate interrupt mask changes with ATA on the same SPEED registers. |
| MDIO and link management | `smap.h:480` declares `smap_mdio_register/unregister`; `smap_open:1137` connects the PHY; link adjustment at 1065 updates EMAC speed/duplex | Import or write the missing MDIO implementation, use bounded PHY operation polling and current phylib APIs, validate the PHY address, and propagate carrier state. The two imported legacy files do not contain that implementation. |
| IOP DMA relay RPC | `smap.h:47` includes obsolete `sifdefs.h`/`dmarelay.h`; `smap_dma_setup:2097` binds four RPC services | Either implement a matching EE/IOP transport and firmware protocol with explicit ownership/cancellation, or start with a genuinely working PIO data path and disable relay DMA. Imported register headers do not implement this transport. |
| Cache maintenance and IOP buffers | TX at 235 and RX at 614 call `ps2sif_writebackdcache`; setup at 2174/2193 translates IOP bus addresses into cached EE mappings | Validate bus address ranges, alignment and lifetimes. Provide correct directional cache synchronization; do not equate an IOP address with an ordinary Linux DMA mapping. |

The modern vendored PS2 snapshot supplies IOP/SIF facilities, but no native SMAP
network driver. `drivers/ps2/iop-dev9.c:247` requests the `dev9` IOP module and
returns immediately at 255. Its later direct initialization and revision
diagnostics are unreachable. This may deliberately delegate power/reset to
firmware, but it must be documented and verified; deleting the early return
without understanding that ownership can reset an active ATA device.

The SDK headers also use a different offset convention. In
`registers/speedregs.h:22`, EE SPEED access is based at `0xb4000000`, while IOP
access is based at `0xb0000000`. `registers/smapregs.h:36` adds `0x100` to the
SPEED base and its SMAP offsets are relative to that new base. The legacy
Linux header instead starts at physical `0x14000000` and includes that `0x100`
in several offsets. Choose one convention consistently; mixing the two shifts
register accesses. The SDK headers depend on `tamtypes.h` and use volatile
pointer access suitable for their SDK context, so they cannot be included as
kernel I/O routines unchanged.

## Concrete defects in the legacy driver

The following issues are observable in the reference source and must not be
carried into the rewritten driver:

- `smap_start_xmit` at 139 returns `-ENETDOWN` and `-EAGAIN`. A modern network
  transmit callback needs the networking transmit result and explicit skb
  ownership. A busy result must retain the skb for the caller; a consumed/drop
  result must account for and release it. It must support non-linear packets
  or explicitly linearize/reject them before copying `skb->data`.
- `smap_probe` at 2407 registers the network device before registering MDIO,
  starting workers and resetting/configuring rings. Users can reach open
  callbacks before that state is ready. Complete initialization first and
  publish the net device last.
- The probe error path at 2447 frees `net_dev` without unregistering a device
  that was already registered. An MDIO failure after registration therefore
  leaves a published freed object. Use staged cleanup that reverses every
  successful acquisition.
- `kthread_run` results at 2419/2421 are not checked with `IS_ERR`. Remove calls
  `kthread_stop` on any non-null pointer at 2464/2469, which includes an error
  pointer. Handle worker creation failures and clear unusable pointers.
- `smap_open` at 1137 stores the result of `phy_connect`. On `IS_ERR`, it jumps
  to `open_error`, which calls `phy_disconnect` on any non-null value at 1182.
  An error pointer is non-null. Preserve `PTR_ERR`, clear the pointer and only
  disconnect a successfully connected PHY.
- RX at 695 writes the obsolete `net_dev->last_rx`; the worker at 2268 uses
  removed `interruptible_sleep_on`. Use the current receive API and a
  condition-based wait/NAPI design that cannot lose a wakeup.
- TX at 254, RX at 634 and DMA setup at 2110 wait indefinitely for RPC
  completion. A missing or stalled IOP service can hang open, traffic or
  removal. Use bounded waits and a transport cancellation rule that prevents
  late callbacks from accessing a returned stack completion.
- RX allocation failure at 684 breaks the delivery loop, but the later
  completion loop at 703 counts descriptors as successfully received even
  when an skb was not allocated. Distinguish delivered, discarded and hardware
  error frames in packet/drop statistics.

Renaming deprecated symbols is insufficient to fix these lifetime and
transport issues. For an initial PIO rewrite, use current platform resources,
managed allocations, current `net_device_ops`, phylib and ethtool link settings,
bounded reset/PHY polling, and a receive budget. Keep slow SIF RPC operations
out of hard IRQ handlers and normal non-threaded NAPI polls. DMA can be added
only after its firmware protocol, cache ownership and cancellation have been
validated on actual consoles.

## Review of the baseline correctness patch

`kernel/patches/driver-correctness.patch` repairs several concrete baseline
defects: error-pointer handling for the IOP module root device, null `ioremap`
handling for OHCI, the GS remove private-data type, PS2 framebuffer removal
ordering, and UART timer/allocation lifetime. These changes should be compiled
and exercised with the actual baseline kernel, including failed probe and
driver removal paths. They do not implement native SMAP or framebuffer pixel
mapping.

The graphics review identified these two concerns:

- In original `ps2fb.c:2377` and `gs_drm.c:711`, a failed `gif_wait` still leads
  to freeing the DMA package page. `drivers/ps2/gif.c:61` returns false after
  polling the DMAC busy bit to its limit; it does not stop the DMA channel.
  Returning `-EBUSY` after releasing that memory cannot prevent DMA from
  accessing a page reused by the kernel. The current correctness patch fixes
  that lifetime error by retaining the package page on timeout and freeing it
  only after a successful drain. This intentionally leaves one page allocated
  on that exceptional path. A future safe channel reset/recovery can reclaim
  it after hardware quiescence is proved.
- Original `gs_probe` at 649 discards the result of
  `gs_fb_helper_initial_config`, and its error cleanup at 676 leaves several
  allocation/DRM cleanup operations commented out. The GS DRM driver needs a
  full failed-probe lifetime review in addition to honest unsupported buffer
  allocation results. It remains an experimental backend.

The framebuffer launcher preflight in the runtime checks RGB555 truecolor at
depth 15 in 16-bit storage and a successful mmap without changing a mode or writing pixels.
This protects the current desktop launch path from the known missing interface;
it does not substitute for implementing CPU backing memory, GS upload/update,
mode setting and scanout ownership in the kernel.

## Hardware acceptance criteria

Native Ethernet is complete only after real hardware has passed EEPROM/MAC
validation, both cold and warm boot, 10/100 link negotiation, DHCP and static
configuration, sustained bidirectional traffic, minimum/maximum packet sizes,
ring wrap, RX allocation failures, cable removal/reconnection, interrupt
sharing with storage, interface stop/restart and module removal. Repeat tests
on each supported PS2 model and on machines without a DEV9 adapter. Capture
logs, packet counters, throughput and memory usage with a 32 MiB kernel.

Host source tests and DHCP command mocks provide useful regression evidence,
but cannot establish electrical bus access, packet DMA coherency or a working
PS2 Ethernet link. The root TODO tracks these unresolved acceptance steps.
