# PS2 kernel patches

Exported patches use hardware names: `ps2`, `PS2_*`, and `ps2-smap`.
Build labels belong in artifact manifests and historical test records.
Patch names and kernel messages do not contain distribution names or
`relay-vN` checkpoint labels. Original upstream copyright notices remain.

The normal source workflow is `scripts/kernel_modernize.py` after the reviewed
platform import. It generates and checks these patches in order:

1. `7.2.8-api.patch`
2. `ps2-smap.patch`
3. `7.2.8-r5900-cp0.patch`
4. `ps2-intc-mask.patch`
5. `ps2-usb-builtin.patch`
6. `ps2-sif-usb-order.patch`
7. `ps2-dma-fault.patch`
8. `ps2-iop-upload.patch`
9. `ps2-user-vm.patch`
10. `ps2-video-memory.patch`
11. `ps2-page-cache.patch`

`ps2-smap.patch` installs the current driver and bounded SIF RPC interface.
It defaults to IOP-owned Ethernet registers/FIFOs and Linux PHY/IP policy.
The platform device, ethtool driver name and Kconfig symbol remain
`ps2-smap`, `ps2-smap` and `CONFIG_PS2_SMAP` respectively.

`ps2-smap-iop.patch` is an alternative upgrade from the preserved, normalized
legacy SMAP source with its cold-start correction. Apply it to that baseline;
it is not an additional patch after the fresh `ps2-smap.patch` installation.
`ps2-smap-startup.patch` records the earlier direct-backend correction.
The head synchronization candidate is a separate development experiment.

`ps2-smap-firmware.patch` targets the pinned IOP modules at commit
`12cdf14b78ed5e648ffe1d52e6754f00b3b69001`. It supplies DEV9 1.2, the
packet RPC service, shared ABI headers, IRQ fixes, freestanding compatibility
headers and `ps2-iop.ld`. The normal firmware builder remains
`scripts/iop_dev9.py`; for a manually patched upstream IOP tree, use the
same qualified R3000 `-O0` flags, `-Icompat`, and `MODULE_LD=ps2-iop.ld`.
Its exported DEV9 library remains version 1.0 for existing ATA imports.

All 19 kernel/emulator patch exports have checked unified-diff syntax and
generic identifiers. The driver, firmware and dependent kernel recipes have
also been applied to isolated baseline trees and checked against their
generated source. See [validation](../../docs/ps2-patch-validation.json).
The user separately confirms basic Ethernet on a physical SCPH-90001 with
the IOP backend: dynamic IPv4 and external ICMP, with zero loss in four packets.
The user also confirms successful `ping gnu.org` and no messages returned
by `dmesg -l err`.
See [physical evidence](../../docs/hardware-network-validation.json).
The remaining platform import gate, other console models and
sustained traffic still require qualification.

## Experimental GS graphics patches

The graphics series applies after the current console/platform sequence above,
including `ps2-video-memory.patch` and `ps2-page-cache.patch`:

1. `ps2-gs-framebuffer.patch` adds a demand-paged CPU upload mapping, with bounds,
   deferred-IO lifetime and VMA ownership tracking. It does not map GS VRAM and
   does not implement hardware rasterization for a software Xorg server.
2. `ps2-gs-commands.patch` adds the experimental `linux/ps2-gs.h` ABI and native
   GS fills, local copies, Gouraud triangles, optional 16-bit depth testing and
   bounded native CT16/CT32 GIF image transfers to the current framebuffer.
   It uses context 2, establishes PRIM attributes explicitly, validates batches
   and waits for a GS FINISH fence. CPU mappings exclude hardware submissions.
3. `ps2-gs-surfaces.patch` adds an exclusive file-owned `/dev/ps2-gs` endpoint,
   bounded page allocation outside front/font/depth reservations, cleared
   CT16/CT32 storage, and typed fills/uploads/local copies. Mode/font/pan changes
   invalidate its handles; disconnect protects surviving open files. Apply it
   after the upload-capable command patch, not to the preserved old GS source.
4. `ps2-gs-rendering.patch` adds allocated Z16/Z16S depth and bounded native
   fill/triangle/depth-clear batches to the same owner. Color/depth roles and
   dimensions are checked before drawing; failed sessions cannot replay work.
   Apply it after the surface patch. Texture sampling remains unsupported.
5. `ps2-gs-textures.patch` adds bounded RGB CT16/CT32 sampling from owned color
   handles into a distinct target. It validates whole triangle batches,
   selects nearest filtering and logical region clamp, establishes perspective
   STQ and invalidates the texture cache before sampling. No texture alpha,
   mipmaps, blending or unrestricted GS addresses are accepted.

These graphics patches are separate from the previously validated 19-patch series.
Their application, exact generated sources, candidate kernel and emulator probe
are recorded in [graphics evidence](../../docs/graphics-validation.json).
The reviewed build is isolated from the working console USB image. It is a GS
diagnostic, not a completed accelerated Xorg/Mesa desktop. Physical graphics
qualification for the corrected primitive scene is user-confirmed.
The user confirmed the upload scene and return to text on the SCPH-90001;
the native Xorg and Mesa drivers remain outstanding. The
upload extension's isolated build and clean patch application are recorded in
[upload evidence](../../docs/graphics-upload-validation.json).

The surface extension and source-only Xorg/Mesa resource patches are recorded
in [surface evidence](../../docs/graphics-surface-validation.json). They do not
enable a complete desktop or texture sampler. Provider lifetime tests use a
syscall/locking scaffold, not a kernel lockdep run.

The owned-rendering extension and source-only Mesa/Xorg ordering components
are recorded in [rendering evidence](../../docs/graphics-render-validation.json).
Ten new host sanitizer and R5900 cases pass. The user confirms the new depth
scene and text restoration in PCSX2; physical owned-rendering testing and the
complete desktop remain pending.

The texture extension and native Mesa sampler/EXA composite components are
recorded in [texture evidence](../../docs/graphics-texture-validation.json).
Eleven new host sanitizer/R5900 cases pass; those tests do not read GPU pixels.
The diagnostic preserves the earlier rendering image and uses a separate
kernel source/output tree. It does not enable a complete Xorg/Mesa desktop.

## IOP LOADFILE memory ownership correction

`ps2-iop-uncached-patch.patch` follows `ps2-iop-upload.patch` and changes the
EE view of IOP RAM to KSEG1. The LOADFILE patch reads and writes complete
words through I/O accessors, publishes its code before the seventh RPC
entry, and removes the whole-IOP cached writeback. A noncoherent replay
reproduces loss of an IOP-updated neighbour with the old code and preserves
it with the correction. Host sanitizer and R5900 checks pass. This is a
correction for a demonstrated memory ownership bug; it does not establish
the cause of the user's physical ROM-module boot stall. Physical retesting
of the new Xorg candidate remains required.
