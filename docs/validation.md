# eeLinux validation evidence

Checks ran on Debian WSL2 and Windows on 2026-10-01 and 2026-10-02. The user
also ran stopping boot checkpoints on a physical SCPH-90001 through LaunchELF
and PS2-to-HDMI. Entry, BSS clearing, decompression, cache handoff and main kernel
entry colors were observed. Those early blank-screen candidates are historical:

the user now confirms Linux text and `/init` after the R5900 CP0 correction.

The built-in USB candidate stopped at IOP IRQ relay loading on physical hardware;
relay-v2 then produced a kernel memory-copy Oops. Its upper fault registers
were unavailable. Relay-v3 prepares receive pages before IOP DMA writes and
prints a compact named fault summary below the trace. USB root subsequently mounted on the user's relay-v5 image; dynamic init
then faulted in `ld.so.1`. Physical login remains unqualified. The earlier DMA
defect has not been established as the later userland panic's cause.

The relay-v3 hardware retest waited for LOADFILE RPC 6 while starting IRQ relay,
with DMA status `0x200044` and an earlier SMAP probe timeout. Relay-v4 replaces
cached IOP firmware stores with SIF DMA, removes synchronous relay-entry output,
adds a 4 KiB RPC stack and exposes startup phases without requiring an IOP reply.

The rebuilt IOP firmware also has aligned file/BSS boundaries and
module sizes that include read-only data and alignment gaps. The unchanged wait prints once, while DMA/client ownership is retained.

The latest toolchain (GCC 16.2.0, binutils 2.47, glibc 2.44) and all 38 base
packages compiled. The updated ext4 root mounts and remounts writable in both
patched emulator hosts. Target PAM rejects the wrong password and accepts

`root` / `eelinux`; account and session checks pass. Thirteen UK console symbols

pass actual `KDGKBENT` readback after loading the upstream map. The user also
confirmed correct keys, root shell access and `ping gnu.org`. Native SMAP obtains
DHCP, supports explicit rebind, resolves DNS and exchanges external ICMP packets
with zero RX/TX errors in the patched Windows emulator. Physical acceptance
remains separate; see [current network evidence](network-runtime-validation.json).

See [authentication evidence](authentication-validation.json),
[keyboard build evidence](keyboard-uk-validation.json),
[IOP firmware evidence](iop-dev9-validation.json) and
[Windows runtime evidence](pcsx2-windows-validation.json).

The current relay-v8 build includes R5900 exception guards, clean-page cache
writeback, corrected folio byte offsets and the resolution selector. Its
private emulator root executes the actual vector inspection, 32 GNU dynamic
loader runs and nine framebuffer previews. The physical relay-v7 root mounted,
then GNU init faulted with `s0=0`. The user now confirms relay-v8 boot and
authenticated GNU root shell on the real SCPH-90001. Native SMAP still fails
probe with MIPS `ETIMEDOUT` (-145). Relay-v9 corrects cold MDIO startup and
reset order, with detailed probe diagnostics; its physical retest is pending.
The earlier loader fault and latest physical observations are recorded in
[hardware userland evidence](hardware-userland-validation.json).

See [video evidence](video-validation.json) for GS memory, EDID, preview and
display-connection limits. The release image contains no private probe modules
or services.

The 2026-10-04 [physical graphics follow-up](graphics-hardware-validation.json)
records the user's confirmation that the ten existing native graphics runtime
diagnostics work on the real PS2, recorded as user-confirmed verification.
This completes normal scene acceptance for those diagnostics. Full Xorg/TWM,
Mesa/GLX application tests and desktop memory measurements remain open; see
[graphics status](graphics.md) and [TODO.md](../TODO.md).

The new native CopyArea stage has twelve host sanitizer and thirty R5900
checks, including solid/GC regressions, and checked generic patches. All
eleven PCSX2 startup command checks pass, the copy probe exits zero and the
user confirms the complete visual sequence. Its new
eleventh runtime scene is separate from the earlier all-ten hardware report;
see [copy evidence](graphics-copy-validation.json).

The actual Mesa Gallium resource copy callback is now connected to that GS
adapter. Seven new callback cases and 39 shared Xorg/context regressions pass
under host ASan/UBSan and in R5900 Linux-user execution. The generic incremental
patch applies and compiles against pinned Mesa. The prior image and hardware
acceptance remain attached to their original binaries; new callback runtime
pixels, full DDX/GLX, TWM and RAM qualification are still open. See
[resource copy evidence](graphics-resource-copy-validation.json).

EXA pixmap copies now have an adapter for `PrepareCopy`, `Copy` and `DoneCopy`
that shares Mesa's GS copy batch. Eight new callback cases and sixteen shared
copy regressions pass under host ASan/UBSan and R5900 Linux-user execution.
The generic Xorg patch applies and compiles against the pinned prerequisite
series. Its checks cover odd-width RGBX/RGB555, overlapping rectangle streams,
preflight, no-op ordering, leases and failure cleanup. New EXA runtime pixels
and upstream window lifecycle remain separate checks. See
[EXA copy evidence](graphics-exa-copy-validation.json).

Gallium's actual `blit` callback now uses native GS nearest scaling, source
flips and destination scissoring. An EXA opaque `PictOpSrc` copy adapter uses
the same public API. Eleven new cases and thirty copy/context regressions
pass under host ASan/UBSan and R5900 Linux-user execution. An independent
rational pixel model checks scaling/flips/scissoring in RGBX/RGB555; EXA
composite pixel cases use RGBX. Both generic patches apply and compile
against their pinned release prerequisite series. See
[blit evidence](graphics-blit-validation.json).

A separate `ps2-gs-blit-test` image now runs actual Gallium blits/resource copies
and EXA composites. Four diagnostic lifecycle cases pass under host sanitizers
and R5900 Linux-user execution, in addition to the 41 component cases. PCSX2
logs all four scenes, successful console restoration and probe exit zero.
Five supplied screenshots match the RGBX scaling, flips, central scissor crop,
identical copies and subsequent overlapping move. This is qualitative panel
review; physical hardware, RGB555 runtime pixels, GPU readback and full Xorg/GLX
remain separate checks. Earlier physical reports retain their original scope.
See [blit runtime evidence](graphics-blit-runtime-validation.json).

## Source and host checks

The current `make check` passed **237 tests**: 206 runtime, archive,
filesystem, userspace ABI and emulator tests, plus 31 kernel integration tests.

The actual adapted `__update_cache` function passes ASan/UBSan visibility checks for a
clean read-only PTE and a dirty four-page folio, including other CPU policy and invalid
PFNs. The deployed object contains the clean-page writeback call and a `PAGE_SHIFT`
byte-offset calculation. The assembled interrupt prologue has `sync.p` before reading
EPC and after writing it.

SHA256 verification passed
for all **437 imported source files**, including the pristine 104-file SysVinit
3.13 release and the modern cache-header and CP0 reference files.

Actual adapted R5900 CP0 functions, INTC/DMAC toggle masks, the SIF DMA writer
and relay preparation run in host C harnesses under ASan/UBSan. Actual RPC
code is replayed against a noncoherent dirty-page model: old code returns stale
data, and the receive-page handoff fix preserves the incoming reply. The dhcpcd
machine-architecture parser and PCSX2 DHCP/RX FIFO handlers are exercised from
their actual source. These tests validate the reported cases, not physical USB
or Ethernet behavior. Actual new C tests also cover chunk/tail bounds, late
firmware DMA, quiet repeated waits, startup preemption/failures and mailbox
detachment before resident entry returns. All six IRX layouts have aligned file/BSS
boundaries and consistent module sizes. A private PCSX2 phase-2 stall test shows
the expected phase after the linker fix, with one warning and one phase report
over 90 seconds; the old image reported phase 0. See
[IOP startup evidence](iop-startup-validation.json), [USB evidence](usb-builtin-validation.json) and
[CP0/hardware evidence](hardware-cp0-validation.json).

The actual relay-v3 register reporter also ran in PCSX2 with synthetic registers:

the eight compact summary lines appeared after the stack/code dump, with correct
addresses, arguments and kernel/module names. Deliberate invalid-copy and BUG
exception probes stalled before reporting, so emulator exception handling is
not qualified by this test. Those private modules are absent from the release
root filesystem. These older probe results do not explain the current
physical relay-v7 GNU loader fault after successful USB/ext4 mounting. See
[panic investigation evidence](hardware-panic-validation.json).

Runtime tests exercise DHCP renewal/deconfiguration, static networking,
ordered SysV services, failure handling, staging rejection, target symlinks
and deterministic filesystem output. Network/service fixtures do not establish
hardware networking. Filesystem unit tests use non-runnable ELF fixtures;
the separately built real base image is recorded below.

Emulator tests verify source-disk preservation, MBR layout, malformed ext2/ELF
rejection, physical/KSEG agreement, entry and 32 MiB RAM bounds, changed-session
rejection and launcher failure reporting. They require explicit R5900 user
emulation and reject replaced emulator binaries. PCSX2 exit status is never
treated as a Linux boot pass.

Both `boot/early-root.c` and the framebuffer interface checker compiled on the
host with `-Wall -Wextra -Werror`. Runtime shell syntax checks passed. All 14
upstream SysVinit programs compiled in a temporary host source copy; the host
PID 1 implementation was not executed.

## GNU builds and packaged base filesystem

The following versions and ext2 artifacts are preserved earlier build evidence.

They do not describe the current ext4/UK/native-SMAP update.

GNU binutils 2.44, GCC 14.4.0 C/C++ and glibc 2.41 built and installed for

`mipsr5900el-unknown-linux-gnu`, using Linux 5.4 userspace headers. Real

C/pthread and C++ links passed the production compiler probe and target
o32/MIPS3/R5900/soft-float ABI audit. Compiler/GAS short-loop workarounds were
checked; runtime atomics still depend on PS2 kernel LL/SC emulation that has
not been verified on hardware.

These versions describe the preserved original build. The requested current
toolchain upgrade uses GCC 16.2.0, binutils 2.47 and glibc 2.44 in a fresh native
Linux prefix; source/download pins are in `toolchain/source-lock.json`.

Completion and rebuilt runtime evidence are recorded separately as they finish.

Real Buildroot configuration resolution passed for `base`, `desktop` and

`full`, with 38, 99 and 116 target packages respectively. **All 38 base target

packages actually built**, along with 33 host packages. The installed tree
passed required GNU-command, 310 Linux ELF, loader and library closure checks.

Its BusyBox binary contains only the selected DHCP helper. Desktop/full are
configured but remain unbuilt. See [userland validation](../userland/validation.json)
and [build details](userland.md).

All 14 SysVinit programs clean-built with the final compiler, totaling 190,640
stripped bytes in the standalone validation. The production Buildroot init is
69,696 bytes; both builds preserve pristine upstream source. Exact hashes and
ABI/dependency results are in [SysVinit validation](sysvinit-validation.json).

| Artifact | Bytes | SHA256 |

| --- | ---: | --- |

| `rootfs.ext2` | 67,108,864 | `8173fda391cb50f6c6512f3d885cf025526dab7575f2192748aa570eb8897dcf` |

| `rootfs.tar` | 43,089,920 | `34e85e7a94757826bcf333fb1a809b03adcc9496598b9753a143fa2da89d2a52` |

The ext2 image passed offline filesystem checking. Tar metadata preserves

`/dev/console` 5:1 mode 0600, account-tool setuid permissions, relative

SysVinit/service links and locked root login. IOP firmware undergoes separate
module-format and manifest-hash checks; its SDK ABI is not accepted as GNU
userspace. See [base filesystem validation](base-rootfs-validation.json).

This base image has no desktop or installed Linux modules, and its account
must be provisioned before login. Disk footprint does not establish RAM usage
or a complete bootable distribution.

The real early-root helper cross-built as a 642,496-byte static R5900 ELF,
passing target ABI and kernel-init checks. Its helper-only newc archive is
644,096 bytes unpacked and 290,278 bytes compressed. Early modules and IOP
firmware are still required for a completed embedded boot filesystem.

## Emulator results

The checksum-pinned frno7 R5900 QEMU 5.0.1 fork built without source patches.

Its `mipsel-linux-user` binary is 3,706,960 bytes, SHA256

`4d0fb1ebf2bc18bf7bd92f329ab070d6d2b032f3ad5c05167325740fec1b31a6`.

**All eight actual smoke tests passed**: signed/unsigned R5900 multiply
GPR/HI/LO checks, Bash arithmetic/builtins, GNU printf, sorting, SHA256,
sed, gawk software floating point and gzip decompression. The report records
exact results and 145 staged loader/library paths whose hashes stayed unchanged.

See [emulator/userland-validation.json](../emulator/userland-validation.json).

Linux-user translates guest syscalls to the host kernel. These checks do not
exercise eeLinux's kernel, PS2 reserved-instruction traps, PID 1, services,
devices, packed MMI instructions or full system memory behavior.

The preset uses 32 MiB RAM, interpreters, disabled speedhacks and guest console logging.
PCSX2 2.6.3 is installed, and its real `-testconfig` check passed with exit code 0 using
a session-local portable runtime/profile. The launcher detects options from installed
`-help`; 2.6.3 has neither `-gamecfg` nor `-datapath`. The profile disables memory cards
and uses local BIOS/NVM copies, preserving existing settings and console state. A
bounded startup test with `Backend=Null` bypassed the host Cubeb failure and reached the
audit ELF entry at 8 MiB, the Linux 7.2.8 banner and 8192-page memory detection. Output
ends at the early-to-dummy console handoff; unsupported CP0/cache notices remain in the
log. ELF loading and early kernel startup were observed; the four userspace milestones
remain unperformed. [Prepared artifact evidence](../emulator/pcsx2-validation.json)
records their hashes. [Emulator instructions](emulator.md) explain launching, stage
evidence and the stock emulator's MMU limitations.

The follow-up `pcsx2-console` session fixes the actual blank screen. A separate
trace located the stall in delay calibration: Timer0 had a pending enabled
interrupt, but CPU Status `0x10020c01` had the Emotion Engine's EIE bit cleared.

The modern adapter now restores the fork's compressed-entry Status
normalization; the configuration builds ps2fb in and retains `ttyS0` alongside

`tty0`. The final clean ELF is **1,925,320 bytes**, has no temporary tracing or

calibration shortcuts, and passes the 32 MiB/decompressor overlap checks.

The installed PCSX2 2.6.3 configuration check passed for that session. Actual
boot completed delay calibration, registered the GS framebuffer and switched
to a **72×28 console at 576×460 PAL**. Windows visual inspection confirmed
readable Linux text, ending at `Waiting for root device /dev/sda1...`.

Serial logging continues after console takeover. The audit uses an empty initramfs and
cannot load the modular USB/storage drivers, so root mount, SysVinit and login remain
unperformed.

## Kernel and hardware acceptance

The selected Linux 7.2.8 `vmlinux`, **43 modules** and compressed audit ELF
cross-link with the Debian MIPS Linux GCC 14.2 audit compiler. The reviewed
modern cache adaptation retains R5900 synchronization/index sequences and
modern exception fixups. Cache assembly was also inspected using the actual
project GCC 14.4 compiler. Exact identities, logs and review evidence are in
[kernel validation](../kernel/validation.json), [modern build](../kernel/modern-build.json)
and [cache review](../kernel/cache-review.json).

The compressed audit ELF is 1,915,656 bytes and loads at physical 8 MiB,
within the checked decompressor/output/RAM layout. This historical artifact
predates the console fix above. The selected audit configurations have no
production early-root/modules/firmware. Decompressor cache/erratum review,
exception/TLB adaptation and context
preservation remain unreviewed; the source gate stays closed with 64 recorded
rejected files. A clean link is not evidence of safe execution.

No completed 7.2.8 boot image, native SMAP Ethernet, GS desktop or measured
32 MiB runtime is claimed. [Hardware details](hardware.md) and root
[TODO.md](../TODO.md) define the remaining implementation and real-console
acceptance criteria.

The native screen `CopyWindow` hook now shares Mesa's GS region copy batch.
Eleven new host sanitizer/R5900 cases cover complete pixel comparisons,
clipping, all overlap directions, redirected RGBX/RGB555 storage, region
translation, no-op queues, failures and callback ownership. Forty-one shared
regressions pass per environment. The R5900 build uses COMPOSITE consistently;
the Xorg 21.1.24 patch also compiles with it disabled. Damage wrapper behavior
is modeled from pinned source; actual Damage/X server execution and new runtime
pixels remain pending. Prior PCSX2 and text-only real hardware reports retain
their original scope. See [CopyWindow evidence](graphics-copy-window-validation.json).

The separate native window diagnostic adds four lifecycle cases per environment
to the 52 preceding component cases. It invokes `ScreenRec.CopyWindow`, seeds
an offscreen pixmap through the Gallium resource copy callback, and presents
through shared native GS transfers. PCSX2 logs all four scenes and exit zero,
including successful console restoration. The user confirms all four RGBX
scene panels and console return, with four supplied screenshots matching the
expected sequence. Physical PS2/RGB555 pixels, the actual Damage subsystem
and full Xorg/TWM lifecycle remain unqualified. All twelve earlier
binaries and prior images are preserved. See
[text-only window runtime evidence](graphics-copy-window-runtime-validation.json).

Native `PaintWindow` adds eleven new host sanitizer/R5900 cases plus 27
related solid/window-copy/context regressions per environment. Independent
pixel references verify child background clipping, nested inherited colors,
borders and redirected RGBX/RGB555 origins; exact Damage coordinates and
unchanged caller regions are checked. Lifecycle, allocation and partial-device
failure checks verify restoration and no replay. The generic Xorg patch
applies and compiles with COMPOSITE enabled and disabled. The actual Damage
subsystem and X server are not executed by these component tests. New emulator/
physical pixels remained pending at that milestone; tiled support is now
checked separately below. The prior text-only hardware report remains limited
to its ten diagnostics. See
[window-painting evidence](graphics-paint-window-validation.json).

Tiled `PaintWindow` and the explicit Mesa repeat adapter add thirteen new host
sanitizer/R5900 cases to 38 regressions per environment. Independent modulo
pixel references verify non-power-of-two tiles, source preservation, inherited
alignment, original border images, redirected RGBX/RGB555, exact Damage and
unchanged caller regions. Small patterns expand through native GS seeding and
local copies; full 720x576 one/two-pixel patterns use at most ten output packets
in the model. Allocation and seed/expansion/later-output failure checks reject
replay. Both generic patches cleanly apply and compile against pinned releases
with COMPOSITE enabled and disabled. New physical qualification and actual
Damage/X server execution remain pending; a separate runtime probe is described below.
See [tiled-painting evidence](graphics-tiled-paint-validation.json).

The new `ps2-gs-paint-test` adds five host sanitizer/R5900 lifecycle checks,
including an independent comparison of every modeled pixel in four scenes.
Solid paint, native tile expansion and presentation failure paths reclaim the
GS session and restore the console without replay; SIGINT restores the screen
hook before teardown. The separate stable-576p image retains thirteen older
diagnostics and runs only the paint probe at boot. Its minimal Damage callbacks
do not execute the actual X server/Damage subsystem. Supplied PCSX2 screenshots
match scenes 2–4: cyan-bordered checker, continuous inherited checker with a
patterned border, and matching front/backing panels. The user confirms the solid
cyan/white-border scene and console return, completing qualitative verification
of all four scenes. Real-hardware and
RGB555 runtime qualification remain separate from these checks.
PCSX2 completes all four command/fence scenes with exit zero and text-mode
restoration in the 32 MiB guest. The exact image, binary, session and log prefix
through completion are identified in
[text-only runtime evidence](graphics-tiled-paint-runtime-validation.json).

Native tiled `GCOps.FillSpans` and `PolyFillRect` add fifteen new host sanitizer/
R5900 cases plus 59 GC, tile-paint, solid-paint, solid, CopyWindow and context
regressions per environment. Independent modulo/Boolean references check
RGBX/RGB555 storage, signed tile phases, non-power-of-two periods, composite
clipping, redirected offsets, rectangle versus pretranslated-span coordinates,
pixel-tile colors/ALUs/masks, queue preservation and GC table revalidation.
Whole-request/capacity rejection precedes output; seed, expansion and later-packet
failure checks reject replay. Damage checks cover the GC composite clip, not an
exact painted footprint. The generic Xorg patch applies to the pinned complete
series and compiles in both COMPOSITE modes. See
[text-only tiled GC evidence](graphics-tiled-gc-validation.json).

`ps2-gs-fill-test` adds five host ASan/UBSan/R5900 lifecycle cases with independent
pixel references for all four modeled scenes. It uses actual native GC callbacks
and shared Mesa adapters, with screen/private/Damage scaffolding. The isolated
stable-576p image retains fourteen older diagnostics and starts only the fill
probe. New physical qualification and actual X server/Damage execution
remain separate; existing hardware and paint-probe qualification is preserved.
No GPU readback or complete desktop RAM measurement is provided by these checks.
PCSX2 completes all four new fill command/fence scenes with exit zero and
text-mode restoration in the 32 MiB guest. The exact run is saved in
[text-only fill runtime evidence](graphics-tiled-gc-runtime-validation.json);
all four supplied PCSX2 panels match the expected scenes, and the user confirms
all four and console return. Physical qualification remains pending.

Opaque stippled FillSpans/PolyFillRect and the explicit Mesa monochrome-to-GS
pattern adapter add fourteen new cases to 74 regressions per environment. Host
ASan/UBSan and R5900 checks pass for independent bit/modulo pixels, both bit
orders, padded source rows, RGBX/RGB555 redirected output, GC revalidation,
negative pattern origins, tiny full-screen expansion and source/input preservation.
Source mutation at the pending-work fence verifies snapshot timing. Invalid
geometry and heap/GS reservations precede flush; native seed/expansion/later
packet failure does not replay or report successful Damage. Native GS builds
colour patterns from at most 8192 snapshot bytes; there is no CPU colour-pixel
expansion. Both incremental generic patch series apply and compile in both
COMPOSITE modes. See [opaque stipple evidence](graphics-stippled-gc-validation.json).

The separate `ps2-gs-stipple-test` adds five host/R5900 lifecycle cases with
independent references for four modeled scenes and no colour-pixel uploads.
Its stable-kernel image preserves fifteen preceding diagnostics. New physical
qualification and real X server/Damage execution remain separate.
That adapter stage consumed existing CPU-resident monochrome sources. Owned
depth-1 storage and GC routing follow below; actual full EXA/fb editing,
transparent stipples and a TWM/GLX session remain unfinished. Previous fill/paint qualification keeps its archived sources.
PCSX2 completes all four new opaque-stipple command/fence scenes with exit zero
and text-mode restoration. [Text-only runtime evidence](graphics-stippled-gc-runtime-validation.json)
pins the exact binary/image/session and successful completion log. All four
supplied PCSX2 panels match the expected scenes, and the user confirms all four
and console return. Qualitative verification stays text-only; real PS2 and
RGB555 runtime pixels remain unqualified.

### Owned monochrome source pixmaps, 2026-10-04

[Monochrome component evidence](graphics-monochrome-validation.json) records
98 checks per environment: ten new lifecycle/storage/source cases and 88 drawing
regressions. Host ASan/UBSan and R5900 checks pass; the generic incremental Xorg
patch cleanly applies and compiles in both COMPOSITE modes. Source bytes are
zeroed, server-padded and capped at 1 MiB/1024 allocations per screen. Native
RGBX/RGB555 output consumes owned source bits while EXA hides the pixel pointer.
Depth-1 lower editing/table replacement is modeled; actual full EXA/fb client
execution is pending. Colour CPU destination rendering remains disabled.

The new owned-source probe adds five host/R5900 lifecycle cases and an isolated
PCSX2 image, preserving sixteen preceding diagnostic binaries and earlier image
files. Command/fence, visual and physical qualification remain distinct. Earlier
opaque-stipple/fill/paint evidence applies to its exact archived sources and has
not been relabeled to qualify the new source-storage callbacks.

PCSX2 completes the four owned-monochrome command/fence scenes with exit zero
and text-mode restoration. Four supplied panels match the expected patterns;
the user confirms all four and console return. Record this qualitative review
as text only in [runtime evidence](graphics-monochrome-runtime-validation.json).
Physical/RGB555 pixels and full X server/fb execution remain unqualified.

### Transparent stipples, 2026-10-04

[Transparent component evidence](graphics-transparent-validation.json) records
114 checks per environment: sixteen new cases and 98 regressions. Host
ASan/UBSan and R5900 checks pass for destination-preserving zero bits,
rectangle/span clipping, redirected RGBX/RGB555, all Boolean ALUs/partial masks,
overlap order, both bit orders, hidden owned sources and pre-fence snapshots.
Invalid geometry, allocation and pixel/run budgets reject before flushing;
first/later packet failures do not replay or report successful Damage.
All-zero patterns and no-op states preserve pending work and skip Damage.
Generic Mesa/Xorg series apply and compile against the pinned releases in both
COMPOSITE modes. Qualified previous reports retain their archived sources.

The distinct transparent probe adds six host/R5900 lifecycle checks with
independent four-scene pixels and first/later command, native Boolean,
presentation and interruption failures. It preserves seventeen previous
binaries and the stable kernel. The exact PCSX2 image/run is qualified
separately from visual review, physical/RGB555 pixels, actual X server/Damage
execution, fine-pattern performance and full desktop RAM.

PCSX2 completes all four transparent-stipple command/fence scenes with exit zero
and text-mode restoration at 70.664 guest seconds. See
[text-only runtime evidence](graphics-transparent-runtime-validation.json). All four supplied panels match the expected scenes, and the user confirms all
four and console return. This qualitative verification stays text-only;
physical/RGB555 pixels and full X server/GLX/TWM use remain unqualified.

### Opaque stipple Boolean operations, 2026-10-05

[Opaque Boolean evidence](graphics-opaque-stipple-logic-validation.json) records
130 checks per environment: sixteen new cases and 114 regressions. Host
ASan/UBSan and R5900 checks pass for disjoint foreground/background ALUs,
all 16 operations and RGB masks, clipped and overlapping requests, redirected
RGBX/RGB555, owned hidden sources and pre-fence snapshots. Constant sources
and equal masked colours coalesce. Work/geometry/allocation rejection precedes
flush; first/later native failure never replays or reports successful Damage.
Optimized opaque copy and transparent fill regressions pass. Generic Mesa/Xorg
patch series apply and compile against the pinned releases in both COMPOSITE
modes. Earlier qualification keeps its exact archived sources.

`ps2-gs-opaque-test` adds six host/R5900 lifecycle cases with independent
four-scene pixels, first/later Boolean, bar/presentation and interruption
failures. Its isolated image preserves eighteen preceding binaries and the
stable kernel. PCSX2 completion, visual acceptance and physical/RGB555 pixels
remain separate from full X server/Damage, fine-pattern performance and full
desktop RAM qualification. No images are added or linked.

PCSX2 completes all four opaque Boolean command/fence scenes with exit zero
and text-mode restoration at 109.3742 guest seconds. See
[text-only runtime evidence](graphics-opaque-stipple-logic-runtime-validation.json). All four panel scenes match the user-provided images; text-console return awaits user confirmation;
physical/RGB555 pixels and full X server/GLX/TWM use remain unqualified.

## Native thin solid line validation, 2026-10-05

[Thin-line evidence](graphics-thin-lines-validation.json) records twelve new
and 130 regression checks passing under host ASan/UBSan and R5900 execution.
The actual pinned Xorg zero-line raster supplies the coverage reference;
independent Boolean pixels validate all octants, screen biases, clipping,
endpoints, XOR joins, redirected RGB555 and partial masks. Complete work and
allocation preflight precedes the original Mesa fence. Colour output uses GS
Boolean runs. Generic patches cleanly apply and compile in both COMPOSITE modes.

The distinct `ps2-gs-line-test` passes six lifecycle cases per environment,
including every modeled scene pixel, first/later native failures and console
restoration. Its isolated image preserves nineteen preceding diagnostics and
the stable kernel. Visual/physical pixels, actual server/Damage execution,
full DDX/Mesa/GLX/TWM and desktop RAM remain unqualified by these modeled checks.

PCSX2 completes all four thin-line command/fence scenes with exit zero and
text-mode restoration at 70.0678 guest seconds. See
[text-only runtime evidence](graphics-thin-lines-runtime-validation.json).
The exact binary/image/session and completion log are preserved. All four
supplied line panels match, and the user explicitly confirms console return.
No images are copied or linked in the documentation. Physical/RGB555 pixels,
actual server/Damage, performance and full desktop use remain separate.

[Probe compatibility evidence](graphics-thin-lines-legacy-probe-validation.json)
records 33 additional host sanitizer cases for the seven older standalone
probe fixtures with the Xorg screen-bias symbol supplied by test bookkeeping.
Production sources match the 142-case component build; prior snapshots and
qualified binaries/images/logs remain unchanged.

[Dashed-line evidence](graphics-thin-dashes-validation.json) records sixteen
new and 142 regression checks passing under host ASan/UBSan and R5900
execution. Unmodified pinned Xorg fbSegment/fbBresDash supplies the independent
coverage/phase reference. Checks cover on/off and double dash, offsets and
clipping, joins/corners/caps, all Boolean ALUs and RGB masks, redirected RGB555,
preflight, bounded work, allocation, fence-time source mutation and failure.
Native GS coloured runs perform colour destination operations. Generic patches
apply and compile in both COMPOSITE modes. Earlier evidence is preserved.

The separate `ps2-gs-dash-test` passes six lifecycle checks per environment,
including all modeled scene pixels and failure/console cleanup. Eight earlier
standalone probes pass 39 additional host sanitizer cases; see
[compatibility evidence](graphics-thin-dashes-legacy-probe-validation.json).
Its isolated image preserves twenty preceding diagnostics and the stable kernel.
PCSX2 completes all four new command/fence scenes with exit zero and text-mode
restoration at 69.5018 guest seconds; exact binary/image/session and log are
preserved in [text-only runtime evidence](graphics-thin-dashes-runtime-validation.json).
All four supplied scene panels match, and the user explicitly confirms console return. GPU pixel readback, physical/RGB555 pixels,
actual server/Damage execution, full DDX/Mesa/GLX/TWM and desktop RAM remain
separate qualifications. No images are added to the documentation.

[Native cursor evidence](graphics-native-cursor-validation.json) records
sixteen new and 158 regression checks passing with host ASan/UBSan and R5900
Linux-user execution. An additional repeat of the sixteen cursor host cases
checks failed-restore CloseScreen unwinding. Unique component cases remain 174.
Independent models cover monochrome pixels and transparency, edge clipping,
hotspots, snapshot ownership, RGB555, pending-queue ordering, native front
read/write exclusion, allocation, lifetime, failure and pointer/Damage callbacks.
The real native callbacks and pinned records compile and execute; MI pointer
and Damage frameworks are stubbed. Generic patches apply and compile in both
COMPOSITE modes. No CPU colour destination renderer or GPU readback is used.

The new `ps2-gs-cursor-test` passes six lifecycle checks per environment,
including every modeled scene pixel, native failures and console restoration.
Nine older standalone probes pass 45 host sanitizer lifecycle checks; see
[compatibility evidence](graphics-native-cursor-legacy-probe-validation.json).
Its isolated image preserves 21 preceding diagnostics and the unchanged kernel.
Full input/VT/X server/Damage execution, visual/physical/RGB555 qualification,
complete DDX/Mesa/GLX/TWM and total desktop RAM remain separate requirements.

PCSX2 completes all four cursor scenes with exit zero and text-mode restoration
at 168.4982 guest seconds; see [text-only runtime evidence](graphics-native-cursor-runtime-validation.json).
All four supplied panels match qualitatively: the small white/black pixelated
arrow is intended, movement leaves no visible trail, the front copy excludes
cursor pixels and the final panels are clean. Explicit user console-return
confirmation is pending. Positions are scripted; USB mouse input and actual
X server/Damage/VT execution are not qualified by this diagnostic.

[Native client-image evidence](graphics-client-images-validation.json) records
sixteen new and 174 regression checks passing under host ASan/UBSan and R5900
execution. Independent expected pixels cover ZPixmap PutImage, source spans,
clipping and padded rows, redirected RGBX/RGB555, Boolean ALUs/masks, overlapping
span order, source mutation at the fence, noops, preflight/heap/run budgets,
sticky failure, GC revalidation, lifetime and cursor overdraw. Real native
callbacks and pinned records compile and execute; MI/Damage bookkeeping remains
stubbed. Generic patches apply and compile in both COMPOSITE modes.

The separate ps2-gs-image-test passes six lifecycle checks per environment,
including every modeled scene pixel, first/later upload failures, completion
bar and presentation failure, interruption and resource/console cleanup.
Ten preceding probes pass 51 additional host sanitizer cases; see
[compatibility evidence](graphics-client-images-legacy-probe-validation.json).
The isolated image preserves 22 previous diagnostics and the unchanged kernel.
GPU readback, visual/physical/RGB555 pixels, actual full X server/input/VT/Damage,
complete DDX/Mesa/GLX/TWM and measured desktop RAM remain separate requirements.

PCSX2 completes all four client-image command/fence scenes with exit zero and
text-mode restoration at 163.0047 guest seconds in a 32 MiB guest. Exact
binary/image/session hashes and the completion-log prefix are preserved in
[text-only runtime evidence](graphics-client-images-runtime-validation.json).
All four supplied panels match qualitatively: shaded source rows, shifted
colours around the red clip gap, white/blue XOR checkers with a green bar, and
clean matching front/backing bars with a white bar. The user confirms all four
scenes and normal text-console return.
This probe invokes native image callbacks with minimal MI/Damage bookkeeping;
actual X server/input/VT, physical/RGB555 pixels, GLX/TWM and total desktop RAM
remain separate qualifications.

[Planar-image evidence](graphics-planar-images-validation.json) records sixteen
new and 190 regression checks passing under host ASan/UBSan and R5900. Native
XYBitmap/XYPixmap callbacks and Mesa source-plane runs are checked against
independent pixels for both bit orders, every legal left pad, clipping and
redirected origins, RGB555, Boolean ALUs/masks, source/header/box mutation at
the fence, noops, allocation and run budgets, late failure, GC revalidation
and cursor overdraw. Pinned fbPutImage and DIX contracts are hashed. Generic
patches apply/equal/compile with COMPOSITE enabled and disabled.

The separate planar diagnostic passes six modeled-scene/lifecycle checks per
environment. Eleven previous probes pass 57 host sanitizer cases; see
[compatibility evidence](graphics-planar-images-legacy-probe-validation.json).
The isolated image preserves 23 earlier diagnostics and the stable kernel.
The request allocation bound is 2621440 bytes, excluding the original client
buffer and allocator overhead; compiled adapter/PutImage/SetSpans frames are
160/192/192 bytes. Full RGB-mask GXcopy uses complete GS colour fills; other
ALUs/partial masks use Boolean runs. These checks use native callbacks and minimal MI/Damage
bookkeeping, not a complete X server. Physical/RGB555 pixels, full
DDX/Mesa/GLX/TWM and measured desktop RAM remain separate qualifications.

The [initial planar visual run](graphics-planar-images-initial-visual-failure.json)
failed scene 4: the backing panel was black. Its exit-zero command/fence result
is retained with the failed sources/image, and does not qualify the corrected
build. The corrected run returns command/fence exit zero at 176.5307 guest
seconds in 32 MiB; see [runtime evidence](graphics-planar-images-runtime-validation.json).
The user confirms all four corrected scenes and normal console return. The
supplied final scene shows matching backing zigzags and a white completion bar;
this is qualitative acceptance without GPU readback.

[Filled-polygon evidence](graphics-filled-polygons-validation.json) records
18 new and 206 regression cases passing under host ASan/UBSan and R5900.
Actual colour FillPolygon callbacks and shared Mesa scan conversion are
compared with pinned Xorg MI pixels for convex, concave and self-intersecting
contours, both fill rules/coordinate modes, clips, redirection, RGB555,
all ALUs/masks and cursor restoration. Source/fence mutation, invalid input,
allocation/run/edge-work limits and sticky failure are exercised.
The driver heap bound is 425984 bytes; compiled Mesa/Xorg frames are 112/4216.
In-place sorting uses no temporary heap. Generic patches compile with and
without COMPOSITE. Six independent diagnostic pixel/lifecycle checks pass
per environment. Actual X server/Damage/input/VT, physical/RGB555 pixels,
patterns, GLX/TWM/OpenGL and total desktop RAM remain separate qualifications.

The polygon diagnostic exits zero at 229.5788 guest seconds in 32 MiB PCSX2;
see [runtime evidence](graphics-filled-polygons-runtime-validation.json).
The four supplied scenes pass qualitative review, including the intentional
even-odd ring versus winding solid rectangle; the user confirms text-console
return. No GPU readback or pixel-by-pixel screenshot comparison was performed.
All 63 preceding host probe checks pass against the new components;
see [legacy probe evidence](graphics-filled-polygons-legacy-probe-validation.json).

[Stippled-polygon evidence](graphics-stippled-polygons-validation.json) records
242 component checks per host/R5900, including 18 new cases against pinned MI
geometry with independent pattern expectations. Coverage includes transparent
and opaque zero bits, owned 1bpp sources, both fill rules/coordinates, pattern
origins, clipping/redirection, all ALUs/masks, RGB555, cursor restoration,
fence-time source/metadata mutation and allocation/run/pixel limits. Six new
diagnostic pixel/lifecycle checks pass per environment, plus
[69 earlier host probe checks](graphics-stippled-polygons-legacy-probe-validation.json).
The driver request heap bound is 696320 bytes, excluding allocator overhead
and original client storage; full desktop peak RAM remains unmeasured.
The new PCSX2 probe exits zero at 174.9583 guest seconds in 32 MiB; see
[runtime evidence](graphics-stippled-polygons-runtime-validation.json).
All four supplied scenes pass qualitative review, with the fill-rule panels
intentionally different. The user confirms normal text-console return.
There is no GPU readback or pixel-by-pixel screenshot comparison.

### First transformed 3D primitive checks

[Transformed triangle evidence](graphics-transformed-triangles-validation.json)
records 261 host ASan/UBSan and R5900 checks: 19 new MVP, projection, clipping,
viewport/depth, colour, no-op, preflight, allocation, snapshot, RGB555, Xorg
ordering and lifetime/failure cases, plus 242 regressions. Six additional
checks per environment verify the exact four-scene diagnostic with a
triangle/depth pixel model confined to tests, presentation failure, interruption
and console/resource cleanup. Measured geometry heap/frame bounds are
196608/1624 bytes. The pinned Mesa component patch applies and compiles in
both COMPOSITE modes. Native transformed 3D acceptance does not qualify
OpenGL, VU shader execution, physical pixels or a full Xorg session.

The new probe exits zero at 209.9709 guest seconds in 32 MiB PCSX2. All four
still scenes pass qualitative review, with user-confirmed text-console return;
see [runtime evidence](graphics-transformed-triangles-runtime-validation.json).
Continuous animation is not part of this fixed-angle diagnostic.
