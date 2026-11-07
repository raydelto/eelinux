# PS2 drawing component patches

`mesa-ps2-raster.patch` adds native GS transport, raster conversion and a
`draw_stage` and native opaque image transfers under Mesa 24.0.9's
`src/gallium/drivers/ps2`.
`xorg-ps2-raster.patch` adds native GS transport and EXA callbacks under
Xorg 21.1.24's `hw/xfree86/drivers/ps2`, including `UploadToScreen`.

These source-only patches do not enable a complete driver. No `pipe_screen`,
complete `pipe_context`, GLX frontend or `ps2_drv.so` is supplied by them yet.
They are separate from the kernel framebuffer and command patches, and their
UAPI header is an exact copy of `kernel/new-drivers/ps2-gs.h`.

The project cross-builds the components into static archives using the real
upstream headers and upstream-generated NIR types. Seventeen callback/packet
cases execute as R5900 Linux programs; host tests additionally use
ASan/UBSan. These are not physical Xorg or OpenGL application tests.

Image transfer requires the `PS2GS_HW_UPLOAD` capability from the current
`ps2-gs-commands.patch`. Old ABI-1 kernels retain primitive support and report
that the new upload feature is unavailable. No raw GS registers, DMA addresses
or VRAM offsets are accepted from clients.

Re-export/check with `build/verification/export-ps2-raster-patches.py` on Linux.
The exporter checks the actual source releases with `git apply --check`,
applies each patch to a fresh directory and compares every resulting file.
See [graphics status](../../docs/graphics.md) and
[recorded checks](../../docs/graphics-development-validation.json).

`xorg-ps2-surfaces.patch` and `mesa-ps2-surfaces.patch` apply after their
respective raster patches. They add the shared owned-surface transport,
native offscreen EXA allocation/drawing callbacks and Gallium color resources.
Their kernel endpoint is supplied by `ps2-gs-surfaces.patch`. They do not enable
a DDX, advertise a texture sampler or finish Mesa/GLX.

Fourteen new host sanitizer and R5900 cases cover storage, transfers, actual
upstream callbacks, provider lifetime with a syscall/locking scaffold and probe
recovery. Export with `build/verification/export-ps2-surface-patches.py`;
clean-series application and built-source equality are checked separately.
See [surface evidence](../../docs/graphics-surface-validation.json).

`mesa-ps2-rendering.patch` and `xorg-ps2-rendering.patch` apply next, after the
matching surface patches. Their kernel requires `ps2-gs-rendering.patch` and
the `PS2GS_HW_OWNED_RENDER` capability. They share the exclusive surface file:
Mesa retains native color/depth resources and submits offscreen triangles;
Xorg fences queued Mesa rendering before EXA drawing and storage changes.
The bridge is an in-process ordering component, not DRI integration.

Build the Mesa component with `USE_GCC_ATOMIC_BUILTINS` and GNU `libatomic`
(`-latomic`) on R5900. The actual atomic resource-reference operations execute
in the target tests; no replacement unlocked reference helpers are used.
Ten new host sanitizer and R5900 cases pass. The user confirms the diagnostic's
cyan-over-red scene, hidden yellow and text restoration in PCSX2. The
2026-10-04 hardware follow-up also confirms the owned-rendering diagnostic
on real PS2 in the user verification report. Later sections cover the
screen/context and texture components; full GLX and the Xorg DDX remain
unfinished. No software pixel fallback is added.

Export/check with `build/verification/export-ps2-render-patches.py`.
Clean series application and source equality are recorded under
`build/desktop/ps2-render-backends/patches.json`.
See [rendering evidence](../../docs/graphics-render-validation.json).

`mesa-ps2-textures.patch` and `xorg-ps2-textures.patch` apply next, after their
rendering patches, and require the kernel's `ps2-gs-textures.patch` capability.
They add shared native RGB sampling, real Gallium sampler views with retained
resource references, a textured final draw stage and native RGB `PictOpSrc`
EXA compositing. Sampling is nearest/clamp-to-edge, level zero, with bounded
normalized coordinates and reciprocal clip W. The adapter scales non-power-
of-two logical coordinates to TEX0's rounded dimensions. GS texture cache
invalidation occurs before every textured triangle.

EXA accepts the native `x8b8g8r8`/`x1b5g5r5` picture layouts, no mask, repeat,
transform or alpha map. It fences Mesa work and restores its target/depth
binding after compositing. Alpha glyphs, other operators and complete DDX
setup remain work. The shared Mesa/EXA API requires a single submission thread.

The archives use the same real upstream headers and GNU atomic support.
Eleven new host sanitizer/R5900 cases cover validation, packets, resource/view
references, upload ordering, callback behavior and actual diagnostic recovery.
Export/check with `build/verification/export-ps2-texture-patches.py`.
See [texture evidence](../../docs/graphics-texture-validation.json).

`mesa-ps2-contexts.patch` and `xorg-ps2-contexts.patch` apply after their
respective texture patches. They add actual Gallium screen/context resource,
surface, framebuffer, sampler, clear/upload/copy/flush callbacks and an Xorg
context-lifetime lease. The narrow opaque context-link header avoids pulling
Gallium internal types into Xorg headers. Both libraries share one submission
thread and the existing owned GS file; no new kernel endpoint is required.

Context and screen destruction wait for live resources, surfaces, views and
Xorg leases. Install the zero-initialized, screen-owned EXA context bridge
after surface preparation and before texture preparation/`exaDriverInit`.
Its cleanup restores the full original callback table before releasing the
lease; do not insert other callback owners while it remains attached. A borrowed
GS render client must outlive all those objects. The standalone screen factory
owns its GS file and closes it only after final reference release.

Shader capabilities remain zero and `draw_vbo` rejects unsupported execution.
The diagnostic uses a bounded adapter for already transformed/clipped triangles;
it does not run an OpenGL application. Native RGB formats, one framebuffer
color target, nearest/clamp sampling and depth clear 1.0 are the current limits.
Six actual callback/lifetime cases and three diagnostic lifecycle cases pass
host sanitizers and R5900 Linux execution. The separate context USB image boots
in PCSX2, with the sixth probe exiting zero. Full DDX, shaders and GLX remain
unfinished, and no software pixel fallback is enabled.

Export/check with `build/verification/export-ps2-context-patches.py`.
Source release checks, clean series application and exact source equality
are recorded in `build/desktop/ps2-pipe/patches.json`.
See [context evidence](../../docs/graphics-context-validation.json).

`mesa-ps2-bitmaps.patch` and `xorg-ps2-glyphs.patch` apply after their respective
context patches. The shared bitmap helper turns existing monochrome font bits
into bounded, clipped native GS fills; it plans the whole request before
submission and restores the Mesa color/depth target after drawing. No new
kernel endpoint or software destination pixel renderer is added.

The Xorg patch supplies actual core `PolyGlyphBlt`/`ImageGlyphBlt` GC callbacks
with server bitmap padding, signed metrics/advances, text background, composite
clip, EXA sync and damage notifications. It accepts native CT16/depth15 and
CT32/depth24 with a full plane mask; transparent text additionally requires
solid `GXcopy`. Image text enforces copy/solid semantics like the upstream
core implementation. Render alpha glyphs and other raster states remain work.

Keep a zero-initialized `ps2_glyph_gc` for each attached GC. Attach after GC
validation/EXA setup, detach before validation/destruction, and do not insert
another operation wrapper while attached. Detach restores the original GC
table and clears its private before releasing the context lease. Closing
contexts reject drawing. Full DDX hooks still need to install this lifecycle.

Eight actual callback/bitmap cases and three standalone probe lifecycle cases
pass host sanitizers and R5900 execution. The pinned Xorg disables Composite,
so redirected-window handling is not qualified; owned offscreen pixmaps are
tested. The probe uses minimal server bookkeeping around actual callbacks,
not a full Xorg/TWM session. Shader caps remain zero and no OpenGL profile is
advertised. Component memory limits are documented separately from unmeasured
desktop RAM usage.

Export/check with `build/verification/export-ps2-glyph-patches.py`.
Source-release checks, clean series application and exact source equality
are recorded in `build/desktop/ps2-glyph/patches.json`.
See [glyph evidence](../../docs/graphics-glyph-validation.json) and
[RAM budget](../../docs/graphics.md#main-ram-budget).

`xorg-ps2-gc.patch` applies after `xorg-ps2-glyphs.patch`. It adds a screen/GC
lifecycle owner and explicit early glyph-private registration. Install its
zero-initialized screen state after EXA/damage setup and before the first
server GC allocation. It handles all GC state/clip lifecycle callbacks and
routes ordinary 8/16-bit text requests through native glyph operations.
It wraps other drawing operations so lower EXA/damage table replacements
cannot silently discard the native hooks. Unsupported raster operations
remain unsupported; no software destination pixel path is added.

Screen cleanup restores all managed GC tables and the screen callbacks before
releasing Mesa leases or invoking lower `CloseScreen`. Other screen wrappers
must unwrap before explicit cleanup. One submission thread and at most 1024
managed GCs are supported. The full DDX must still install this component;
it does not supply screen/cursor/VT initialization or Mesa shaders/GLX.

Eight lifecycle and three actual diagnostic cases pass host ASan/UBSan and
R5900 execution; eleven host glyph regression cases also pass. Mutable lower
table fixtures model upstream contracts rather than running a complete Xorg
server. The probe checks early private registration and GC destruction and
uses real native rendering through ordinary text requests.
Earlier glyph inputs are preserved in `build/desktop/ps2-glyph/source`.

Export/check with `build/verification/export-ps2-gc-patch.py`.
The exporter applies the prerequisite series to a copy of the actual pinned
Xorg release before checking the new patch, and separately checks a clean
series and exact built-source equality. See [GC evidence](../../docs/graphics-gc-validation.json).

The standalone native GC diagnostic exits zero in the controlled PCSX2 run,
with all eight startup command checks passing. Its three-row pattern uses an
intentional clip gap over `S`/`T`. This is component evidence; the full Xorg
screen, Mesa shader pipeline and GLX application path remain unfinished.

## Native constant-source Boolean drawing

`mesa-ps2-logic.patch` follows the bitmap series and adds the shared native
logic transport and an explicit color-resource adapter. `xorg-ps2-logic.patch`
follows the GC series and adds EXA solid drawing with all sixteen ALUs and RGB
plane masks. Both require `ps2-gs-logic.patch` after the kernel texture series,
and check `PS2GS_HW_SOLID_LOGIC` before exposing the new path. Alpha is always
preserved. Constant copies and masked RGB inversion use at most two GS
sprites, with no CPU destination rendering or readback.

Install a zero-initialized `ps2_logic_exa` after the EXA context/texture bridge
and before `exaDriverInit`. Cleanup must restore its two callbacks and release
its lease before cleaning the outer context bridge. One submission thread is
supported. Other wrappers must unwrap before explicit cleanup. Queued Mesa
work is fenced before drawing; color/depth bindings remain intact.

Thirteen host sanitizer and R5900 cases pass, covering raw packets against an
independent Boolean oracle, owned/depth/stale/error validation, actual EXA/Mesa
callbacks, allocation lifetime and native diagnostic cleanup. The isolated
image is a component test; source-pixmap logic operations,
full DDX setup, shaders and GLX remain unfinished. Export/check with
`build/verification/export-ps2-logic-patches.py`. The exporter applies all
prerequisites to copies of the actual pinned sources and checks clean series
application and source equality. See [logic evidence](../../docs/graphics-logic-validation.json).

The ninth PCSX2 startup probe exits zero, and the user confirms the expected
sixteen-tile grid, outline appearance/erasure and console return. That CT32
diagnostic scene has visual evidence. The 2026-10-04 all-tests hardware report
also confirms it on real PS2. RGB555/partial masks and
a complete TWM session remain unqualified.

## Native solid GC requests and shared Mesa batches

Apply `mesa-ps2-solid-batch.patch` after `mesa-ps2-logic.patch` and
`xorg-ps2-solid-gc.patch` after `xorg-ps2-logic.patch`. Kernel native solid
logic support is required. The Mesa adapter validates every bounded rectangle
before flushing the shared queue and retains the color/depth bindings and
resource references through one submission lease. It remains an explicit
native operation; shader/GLX and OpenGL logic-op capabilities are not enabled.

The Xorg patch adds optional solid hooks to the existing GC lifecycle and
routes six actual callbacks: spans, points, thin axis-aligned solid polylines,
segments, rectangle outlines and filled rectangles. Every clipped request is
planned before submission. All sixteen constant-source ALUs and RGB masks are
handled by the GS; unsupported raster states do not use a CPU pixel fallback.
Thin corners and endpoint caps retain XOR behavior. Successful requests report
EXA synchronization and conservative composite-clip damage.

Install `ps2_solid_screen_prepare` after GC screen preparation; keep its state
alive until cleanup. Explicit cleanup must precede GC screen cleanup; wrapped
`CloseScreen` restores the solid callbacks first and then calls the GC close.
Other screen wrappers must unwrap first. Use one submission thread. Existing
validated GCs and later GC validations both retain the native solid hooks.

Thirteen new host/R5900 checks and eleven GC/core-text regressions pass. The
exporter checks prerequisite application on the actual pinned releases and a
clean series, then exact source equality and cross-compilation of the patched
component sources without project component include paths. The Xorg patch
includes the public Mesa batch header it consumes. The separate solid GC diagnostic
uses a panel and three reversible outlines, rather than a complete X server.
Diagonal/wide/dashed lines, further tile ALUs/masks, alpha glyphs, full DDX setup and Mesa
shader/GLX execution remain unfinished. Earlier GC build inputs and USB images
are preserved. See [solid GC evidence](../../docs/graphics-solid-validation.json).

Export/check with `build/verification/export-ps2-solid-patches.py`.

All ten startup command checks pass in the controlled PCSX2 run and the solid
GC probe exits zero. The supplied panel screenshot matches the intended scene;
the user confirms all three XOR outlines erase and the text console returns.
This qualifies that CT32 scene in PCSX2. The 2026-10-04 all-tests report also
confirms it on real PS2. RGB555/partial-mask pixels
and a full TWM/OpenGL session still need qualification.

## Physical diagnostic follow-up

On 2026-10-04 the user confirms that all ten current graphics runtime
diagnostics work on the real PlayStation 2. The
[hardware acceptance record](../../docs/graphics-hardware-validation.json)
records the dated user verification and lists the ten accepted diagnostics.
The earlier component records keep their build/emulator measurements and link
this dated physical follow-up. This closes normal hardware scene testing for
the present components; full Xorg/TWM, Mesa/GLX application tests, RAM
measurements and hardware failure/recovery testing remain open.

## Native CopyArea and Mesa region copies

Apply `mesa-ps2-copy-batch.patch` after `mesa-ps2-solid-batch.patch` and
`xorg-ps2-copy.patch` after `xorg-ps2-solid-gc.patch`. No new kernel ABI is
needed; existing surface transfers and texture triangles are used.

Install `ps2_copy_screen_prepare` after GC/solid preparation and before using
native CopyArea. Keep its state alive until cleanup. Cleanup restores the
copy hooks before solid/GC cleanup; wrapped `CloseScreen` follows that order.
Other wrappers must unwrap first. Use the same exclusive GS file and one
submission thread. Source validation and exposures use upstream Xorg
`miDoCopy`, while its copy callback submits only GS drawing.

The shared Mesa API `ps2_pipe_copy_boxes` accepts up to 256 canonical banded
rectangles with one translation, at most 1,048,576 copied pixels. It validates
the complete request before flushing queued drawing, preserves target/depth
bindings and references, and orders overlap like `memmove`. Aligned spans use
native local transfers. Unaligned tails use one 64x32 scratch surface (8 KiB
of GS memory in CT32 or CT16) and native texture sampling. Only RGB555/RGBX
copy semantics are claimed; the unused alpha storage is not an application
alpha channel. Xorg accepts `GXcopy` with a full RGB plane mask, zero RGB mask,
and `GXnoop`; other copy ALUs/masks terminate without a software pixel fallback.

This stage introduced an explicit native Mesa adapter. Its original archives
and diagnostic image retain the earlier Gallium copy callback; apply the
subsequent callback patch below to remove its aligned-width restriction.
Shader execution, `draw_vbo`, GLX and a complete Xorg DDX remain unfinished.

Twelve new host ASan/UBSan cases and thirty R5900 cases pass, including ten
solid and eight GC regressions. The copy tests link the actual pinned
upstream `mi/micopy.c`; diagnostic exposure/event bookkeeping is minimal and
is not an X server. The exporter checks clean prerequisite application,
exact source equality and patched component compilation against the pinned
Mesa/Xorg releases without project component include paths.

Build/export/package with `build/verification/build-ps2-copy.py`,
`export-ps2-copy-patches.py` and `package-ps2-gs-copy.py`. The isolated image
adds `ps2-gs-copy-test` after the ten previously qualified diagnostics and
retains their original binaries and the stable 576p kernel. The new diagnostic
shows two identical bars/checker panels, an overlapping down/right move, then
a split right panel with a narrow clip gap, before restoring the text console.
Its physical verification is pending; the earlier all-ten hardware report
does not qualify this new eleventh test. See
[copy evidence](../../docs/graphics-copy-validation.json).

The requested PCSX2 console session completes all eleven startup command
checks; CopyArea exits zero at guest time 105.6865 seconds. The user confirms
the matching initial panels, overlapping move, clipped gap and console return.
Physical qualification of this new eleventh diagnostic remains pending.

## Gallium resource copy callback

Apply `mesa-ps2-resource-copy.patch` after `mesa-ps2-copy-batch.patch`.
The actual `pipe_context.resource_copy_region` callback now submits through
the GS batch adapter for same-format RGBX/RGB555 2D color resources at level
and layer zero, with positive boxes of depth one. Ownership and complete
bounds validation precede queue flush. Odd-width tails use the same GS
scratch/sampling path; framebuffer/depth/sampler bindings and references are
preserved. Invalid requests set the sticky context error without submitting
queued work, and hardware errors are never replayed.

Link `ps2_copy_batch.o` into any Mesa archive containing `ps2_pipe.o`.
The project builders and host fixtures now include that dependency exactly
once. The Xorg callback source is unchanged in this milestone and continues
to share the context through the existing copy patch. No additional kernel
ABI or software pixel path is introduced.

Seven new host sanitizer/R5900 cases and 39 shared context/Xorg regressions
pass in each environment. The new exporter applies the full prerequisite
series, compares the patched source and compiles both the callback and its
batch dependency against pinned Mesa without project component include paths.
Build and export with `build/verification/build-ps2-resource-copy.py` and
`export-ps2-resource-copy-patch.py`. See
[callback evidence](../../docs/graphics-resource-copy-validation.json).

Earlier image/source identities are preserved. The rebuilt standalone
CopyArea diagnostic verifies linking and lifecycle regression; it calls
the Xorg copy path, so it does not qualify pixels through the newly connected
Gallium callback. Callback runtime qualification, shader/state `draw_vbo`,
GLX and the full Xorg/TWM desktop remain unfinished.

## EXA copy callbacks

Apply `xorg-ps2-exa-copy.patch` after `xorg-ps2-copy.patch`. It adds
`ps2_copy_exa.c/h` under `hw/xfree86/drivers/ps2`, using the existing Mesa
`ps2_pipe_copy_boxes` API from `mesa-ps2-copy-batch.patch`. The Mesa GS transport
and kernel ABI are unchanged.

Install zero-initialized `ps2_copy_exa` state after the context/texture/logic
EXA bridges, before `exaDriverInit`. Retain state and both pixmaps through
`DoneCopy`. Restore these three callbacks before the outer context bridge;
cleanup checks callback ownership and releases its context lease only after
restoration. One submission thread is required.

GXcopy accepts full RGB masks in native depth24/CT32 and depth15/CT16 storage.
Odd widths and per-rectangle overlap use native transfers and optional 8 KiB
GS scratch sampling. GXnoop/zero RGB masks, empty and identity copies do not
flush pending Mesa commands. Other copy ALUs/masks remain unsupported; the
existing native `PrepareAccess` terminates CPU drawing. Each Copy rectangle
is checked before submission; EXA owns clipping and inter-rectangle order.

Eight new sanitizer/R5900 cases and sixteen shared copy regressions pass in
each environment. The generic patch's full pinned Xorg 21.1.24 series applies
and compiles using patched component headers. Export/build with
`build/verification/export-ps2-exa-copy-patch.py` and
`build-ps2-exa-copy.py`; host checks use `check-ps2-exa-copy-host.py`.
See [EXA copy evidence](../../docs/graphics-exa-copy-validation.json).

This is component integration. New EXA copy callback runtime pixels, full
X server lifecycle and `ps2_drv.so`, Mesa shaders/`draw_vbo`, GLX and the first
`startx`/TWM session remain open. Prior diagnostic images and physical
verification keep their original scope.

## Gallium blit and EXA source composites

Apply `mesa-ps2-blit.patch` after `mesa-ps2-resource-copy.patch`. It adds
`ps2_blit.c/h` and connects Gallium's actual `blit` callback. Apply
`xorg-ps2-blit.patch` after `xorg-ps2-exa-copy.patch`; it adds
`ps2_blit_exa.c/h` and the narrow public blit header under
`hw/xfree86/drivers/ps2`. Link the native Mesa implementation with Xorg's
existing shared context/transport components. The kernel ABI is unchanged.

Gallium supports nearest scaling, source flips and destination scissoring
for matching RGBX/CT32 or RGB555/CT16 resources. Unscaled transfers reuse the
copy batch for odd widths and overlap. Scaled/flipped sources must be owned,
distinct textures of at most 1024 by 1024 pixels. Partial RGB masks, linear
filtering, blending, depth/stencil, MSAA and nonzero levels/layers are rejected.
Empty regions and unused-alpha/zero masks preserve pending commands.

The EXA adapter implements opaque `PictOpSrc` copies in matching native
picture layouts with no mask/repeat/transform/alpha map and nearest filtering.
Xorg scaling/transforms remain pending. Install zero-initialized screen-owned
state after context/texture setup, before `exaDriverInit`; retain prepared
pixmaps through `DoneComposite`. Restore all four composite callbacks before
the outer context bridge, removing any later composite wrapper first. One
submission thread is required. Geometry/storage checks precede queue flush,
and a context lease protects lifetime. Native `PrepareAccess` prevents CPU
pixel fallback.

Eleven new sanitizer/R5900 cases and thirty regressions pass in each
environment. Both complete prerequisite patch series apply and compile
against their pinned releases using patched component headers. Use
`build/verification/build-ps2-blit.py`, `check-ps2-blit-host.py`,
`export-ps2-blit-patches.py` and `record-ps2-blit-progress.py`. See
[blit evidence](../../docs/graphics-blit-validation.json).

The separate `ps2-gs-blit-test` image now exercises actual Gallium blits/resource
copies and the new EXA composite hooks in RGBX/CT32. Four additional lifecycle
checks pass per environment. PCSX2 logs all scenes and probe exit zero; five
supplied screenshots match the expected panels. See
[runtime evidence](../../docs/graphics-blit-runtime-validation.json).
Physical/RGB555 runtime qualification remains open. Earlier qualified images
and archives are preserved. Full `ps2_drv.so`, Mesa
shader/vertex execution and `draw_vbo`, GLX, the first `startx`/TWM session and
desktop RAM measurements remain open.


## Native screen CopyWindow

Apply `xorg-ps2-copy-window.patch` after `xorg-ps2-blit.patch`. It adds
`ps2_copy_window.c/h` under `hw/xfree86/drivers/ps2`, using the already exported
public Mesa context lease and copy batch interfaces. The native screen hook
clips and translates Xorg regions, uses front/owned RGBX or RGB555 GS storage,
accounts for COMPOSITE backing pixmap origins, and submits the complete request
through the existing Mesa batch. The lower raster hook is saved for restoration
and is never invoked to draw pixels.

Install the zero-initialized screen owner after `exaDriverInit`, before
`DamageSetup` (also before cursor helpers that initialize Damage). Keep Damage
notifications in the upstream wrapper; register native GC owners after Damage.
Cleanup follows reverse callback nesting before releasing the context lease.
Use consistent COMPOSITE record settings in all translation units.

The complete pinned Xorg 21.1.24 patch series applies and the new production
component compiles with COMPOSITE enabled and disabled, without repository
component include paths. Eleven new sanitizer/R5900 cases and 41 shared
regressions pass per environment. Pixel comparisons use an independent source
snapshot; Damage wrapper behavior is modeled, not full subsystem execution.
Separate archives and exact inputs are under `build/desktop/ps2-copy-window`.
Build/export/check/record scripts are `build-ps2-copy-window.py`,
`export-ps2-copy-window-patch.py`, `check-ps2-copy-window-host.py` and
`record-ps2-copy-window-progress.py` under `build/verification`. See
[CopyWindow evidence](../../docs/graphics-copy-window-validation.json).

The new hook now has a separate runtime diagnostic. Its PCSX2 command checks
pass, with user-confirmed panels and console return; physical pixels and
complete X server/TWM window lifecycle integration remain pending. Full DDX, Mesa shader/vertex/`draw_vbo`, GLX and
real desktop RAM measurements remain open. Existing runtime images and the
text-only physical report retain their original verification scope.


The new `ps2-gs-window-test` probe executes the actual installed native window
hook with minimal screen bookkeeping and shared Mesa transfers. Its separate
image preserves the stable 576p kernel and twelve previous binaries. Four
lifecycle checks pass per environment; all four PCSX2 scenes complete with exit
zero and console restoration. Probe sources, tests and immutable logs are under
`build/desktop/ps2-copy-window/probe`. Build with `build-ps2-window-probe.py`,
check with `check-ps2-window-probe-host.py`, package with
`package-ps2-gs-window.py`, then record with `record-ps2-window-runtime.py` under
`build/verification`. See
[text-only runtime evidence](../../docs/graphics-copy-window-runtime-validation.json).
The user confirms all four scene panels and console return; four supplied
screenshots match the expected sequence. This does not execute a complete
X server/Damage subsystem; physical/RGB555 qualification remains open.


## Native solid window painting

Apply `xorg-ps2-paint-window.patch` after `xorg-ps2-copy-window.patch`. It adds
`ps2_paint_window.c/h`, installing `ScreenRec.PaintWindow` for solid background
and border pixels through the shared Mesa Boolean batch. Nested
`ParentRelative` backgrounds inherit color/inhibition from the resolved
ancestor while retaining the child's clip; borders draw to the backing
pixmap. RGBX/RGB555 storage, redirected origins and Damage coordinates are
validated. None, inhibition and empty intersections leave the queue untouched.
This preserved solid-only patch rejects tiled painting; the later tiled
patch below extends the current hook.

Install the zero-initialized screen owner after EXA and before later screen
wrappers. Damage must be initialized before drawing. Unwrap later owners
before cleanup; the lease is released only after both callbacks are restored.
The region and 4096-byte maximum plan are validated before drawing; partial
device failures are sticky and never replayed. The saved lower painter does
not draw pixels. The generic patch cleanly applies to the complete pinned
Xorg 21.1.24 series and compiles with COMPOSITE enabled and disabled.

Eleven new cases and 27 related regressions pass per environment under host
ASan/UBSan and R5900 Linux-user execution. Exact snapshots/archives/logs are in
`build/desktop/ps2-paint-window`; build, check, export and record with
`build-ps2-paint-window.py`, `check-ps2-paint-window-host.py`,
`export-ps2-paint-window-patch.py` and `record-ps2-paint-window-progress.py` in
`build/verification`. See
[text-only painting evidence](../../docs/graphics-paint-window-validation.json).
Actual Damage/X server execution, new emulator/hardware pixels and the complete
DDX/Mesa/GLX desktop remain pending; tiled support is checked separately below. No new runtime image was
packaged; earlier diagnostic and hardware evidence stays unchanged.


## Native tiled window painting

Apply `mesa-ps2-tile.patch` after `mesa-ps2-blit.patch` and
`xorg-ps2-tiled-paint.patch` after `xorg-ps2-paint-window.patch`. The Mesa
patch adds the explicit `ps2_pipe_tile_rectangles` adapter; the Xorg patch
extends `PaintWindow` and copies its public header. Distinct owned native
RGBX/RGB555 tiles repeat from the resolved background ancestor's origin,
including borders, whose image still comes from the original window.
Non-power-of-two periods, signed origins, child clipping and redirected
backing offsets are supported. Existing precise Damage/lifetime contracts
remain intact. Gallium repeat sampler/shader support is not advertised.

Small patterns seed and expand a GS scratch block of at most 64x32 logical
pixels using texture draws and native local copies. Work is preflighted and
scratch reserved before flushing. Limits are 256 boxes, 1,048,576 pixels and
65,536 output pieces, with 64 triangles per packet. Source/front aliases and
incompatible storage are rejected; GS VRAM capacity applies. Scratch is freed
on success and reclaimed by file closure after sticky device failure.

Thirteen new and 38 related regression cases pass on host ASan/UBSan and
R5900. Both complete patch series cleanly apply and compile against pinned
Mesa 24.0.9/Xorg 21.1.24 with COMPOSITE enabled and disabled. Archives, tested
source snapshots and logs are in `build/desktop/ps2-tiled-paint`; reproduce with
`build-ps2-tiled-paint.py`, `check-ps2-tiled-paint-host.py`,
`export-ps2-tiled-paint-patches.py` and `record-ps2-tiled-paint-progress.py` under
`build/verification`. See [text-only tiled-painting evidence](../../docs/graphics-tiled-paint-validation.json).
The prior solid milestone, earlier images and physical report are preserved.
Physical pixels, actual X server/Damage integration and the
complete DDX/Mesa/GLX desktop remain pending.

The separate `ps2-gs-paint-test` diagnostic invokes this native hook and Mesa's
tile/solid adapters in four scenes. Five host ASan/UBSan and R5900 lifecycle
checks compare modeled scene pixels and verify failure/interrupt cleanup.
`build/images/ps2-gs-paint/ps2-gs-paint-usb.img` retains thirteen preceding
diagnostics and automatically runs only the new probe with the unchanged
stable-576p kernel. Exact sources and logs are in
`build/desktop/ps2-tiled-paint/probe`; build, check and package with
`build-ps2-paint-probe.py`, `check-ps2-paint-probe-host.py` and
`package-ps2-gs-paint.py` under `build/verification`. The probe's Damage callbacks
are stubs; actual X server/Damage and physical qualification remain open.
Supplied PCSX2 screenshots match scenes 2–4 qualitatively; the user confirms
solid scene 1 and console return, completing qualitative verification of all
four scenes. Images are not added to the repository.
PCSX2 completes all four paint scene command checks with exit zero and text-mode
restoration. See [text-only runtime evidence](../../docs/graphics-tiled-paint-runtime-validation.json).

## Native tiled GC fills

Apply `xorg-ps2-tiled-gc.patch` after `xorg-ps2-tiled-paint.patch`. It adds
`ps2_fill.c/h` and a fill override in the GC registry, reusing the existing Mesa
tile and solid-batch adapters. Install after native GC/solid setup; restore the
fill registration before solid/GC teardown. GC revalidation preserves CopyArea,
glyph and other solid callbacks. These components still require full DDX setup.

Actual FillSpans and PolyFillRect support owned RGBX/RGB555 tile pixmaps,
non-power-of-two periods, GC patOrg, composite clipping and redirected offsets.
Spans are already translated; rectangles honor miTranslate. Pixel tiles use
`tile.pixel` with native constant-source ALUs/masks. Pixmap tiles require
GXcopy/full RGB masks; GXnoop/zero-mask requests retain queued work. Front/alias/
foreign/stale sources and unsupported states fail explicitly. The request is
preflighted with at most 4096 inputs and 256 output boxes/4096 plan bytes; native
GS scratch, piece and pixel bounds remain those of the Mesa tile adapter.

Fifteen new and 59 regression cases pass on host ASan/UBSan and R5900. The full
Xorg patch series applies cleanly and the components compile against Xorg 21.1.24
with COMPOSITE enabled and disabled. Archives, exact sources and logs are under
`build/desktop/ps2-tiled-gc`; build/check/export/record scripts are
`build-ps2-tiled-gc.py`, `check-ps2-tiled-gc-host.py`,
`export-ps2-tiled-gc-patch.py` and `record-ps2-tiled-gc-progress.py` in
`build/verification`. See [text-only tiled GC evidence](../../docs/graphics-tiled-gc-validation.json).

The separate `ps2-gs-fill-test` diagnostic creates/revalidates actual native GCs
and calls the shared adapters in four scenes. Five additional host/R5900
lifecycle checks exercise independent modeled pixels and failure/interrupt
cleanup. Build/check/package with `build-ps2-fill-probe.py`,
`check-ps2-fill-probe-host.py` and `package-ps2-gs-fill.py`. The image is
`build/images/ps2-gs-fill/ps2-gs-fill-usb.img`; it preserves fourteen preceding
diagnostics and the stable-576p kernel. Screen/Damage bookkeeping remains
minimal scaffolding. New physical qualification, actual X server/Damage,
stippled fills, full DDX/Mesa/GLX and desktop RAM measurement remain open.
The older qualified paint probe keeps its exact archived GC layout and evidence.
PCSX2 completes all four fill command/fence scenes with exit zero and text-mode
restoration. See [text-only fill runtime evidence](../../docs/graphics-tiled-gc-runtime-validation.json);
all four supplied panels match the expected scenes, and the user confirms all
four and console return. This qualitative verification is recorded without
adding or linking images.

## Native opaque stippled GC fills

Apply `mesa-ps2-stipple.patch` after `mesa-ps2-tile.patch` and
`xorg-ps2-stippled-gc.patch` after `xorg-ps2-tiled-gc.patch`. The Mesa patch extends
its existing tile adapter with monochrome source snapshotting and native GS
foreground/background pattern construction. The Xorg patch extends the existing
FillSpans/PolyFillRect override and includes its updated public Mesa header.
No additional installation owner or kernel ABI change is required.

Existing server 1bpp source pixmaps up to 256x256 are accepted for GXcopy/full RGB
masks; no-op/zero-mask requests preserve the queue. Destination colour pixels
stay on GS. Pattern origins, composite clipping and redirected RGBX/RGB555 keep
the tile contracts. At most 8192 source bytes are snapshotted. Native pattern
and optional expansion resources are reserved before the fence, then released
on success or reclaimed on failed-session closure. Transparent stipples and
non-copy ALUs/partial masks are unsupported. The owned source-only allocation/GC routing patch follows below; actual
full EXA/fb client drawing remains open, and this adapter does not enable the DDX.

Fourteen new and 74 regression cases pass on host ASan/UBSan and R5900. Both
complete generic series apply and the components compile against pinned Mesa
24.0.9/Xorg 21.1.24 in both COMPOSITE modes. Archives/snapshots/logs are under
`build/desktop/ps2-stippled-gc`; build/check/export/record scripts are
`build-ps2-stippled-gc.py`, `check-ps2-stippled-gc-host.py`,
`export-ps2-stippled-gc-patches.py` and `record-ps2-stippled-gc-progress.py`.
See [text-only opaque stipple evidence](../../docs/graphics-stippled-gc-validation.json).

The separate `ps2-gs-stipple-test` adds five lifecycle cases per environment;
its four scenes construct all colour patterns on GS without colour-pixel uploads.
Build/check/package with `build-ps2-stipple-probe.py`,
`check-ps2-stipple-probe-host.py` and `package-ps2-gs-stipple.py` in
`build/verification`. The isolated image preserves fifteen earlier diagnostics
and the stable kernel. New physical pixels and actual X server/Damage/
TWM/GLX use remain separate. Qualified fill/paint evidence stays unchanged.
PCSX2 completes all four opaque-stipple command/fence scenes with exit zero and
text-mode restoration. See [text-only runtime evidence](../../docs/graphics-stippled-gc-runtime-validation.json);
all four supplied PCSX2 panels match the expected scenes, and the user confirms
all four and console return. This qualitative verification is text-only;
physical qualification remains pending.

## Owned source-only monochrome pixmaps

Apply `xorg-ps2-monochrome.patch` after `xorg-ps2-stippled-gc.patch`, retaining
the existing Mesa stipple/tile dependencies. The EXA driver owns zeroed,
server-padded depth-1 bits with a 1 MiB pixel-byte budget and 1024-pixmap limit.
The source view remains available to native fills while EXA hides the CPU pointer;
depth-1 GCs preserve lower bitmap editing callbacks and table/lifetime contracts.
Colour operations retain native GC hooks and fatal CPU access. Full EXA/fb client
bitmap upload/drawing remains unqualified until a real X server session.

Ten new and 88 regression cases pass on host sanitizers and R5900. The generic
patch cleanly applies and compiles against pinned Xorg 21.1.24 with COMPOSITE
on/off. Archives, sources and logs are under `build/desktop/ps2-monochrome-pixmaps`;
see [component evidence](../../docs/graphics-monochrome-validation.json).
Build/check/export/record helpers are `build-ps2-mono.py`,
`check-ps2-mono-host.py`, `export-ps2-mono-patch.py`, and `record-ps2-mono-progress.py`.

The separate `ps2-gs-mono-test` has five host/R5900 lifecycle checks, allocates
its source bits through driver callbacks and preserves sixteen earlier binaries.
Build/check/package/record helpers are `build-ps2-mono-probe.py`,
`check-ps2-mono-probe-host.py`, `package-ps2-gs-mono.py` and `record-ps2-mono-runtime.py`.
PCSX2, physical, full DDX/GLX/TWM and desktop RAM qualification remain separate.

PCSX2 completes all four owned-monochrome command/fence scenes with exit zero
and text-mode restoration. The supplied panels match, and the user confirms
all four and console return. Preserve [text-only runtime evidence](../../docs/graphics-monochrome-runtime-validation.json)
without adding images; physical and actual X server qualification remain open.

## Transparent stippled GC fills

Apply `mesa-ps2-transparent-stipple.patch` after `mesa-ps2-stipple.patch` and
`xorg-ps2-transparent-stipple.patch` after `xorg-ps2-monochrome.patch`.
The shared adapter emits native GS rectangles for one-bit source runs and
preserves zero-bit destinations. It supports all 16 Boolean ALUs/partial RGB
masks, clipping and redirected RGBX/RGB555, with a bounded source snapshot and
exact output-work preflight. It adds no OpenGL capabilities or CPU colour path.
GXcopy/full masks allow 262144 runs; Boolean/partial masks allow 16384.

Sixteen new plus 98 regression cases pass on host sanitizers and R5900.
Both complete generic series apply and changed components compile against
pinned Mesa 24.0.9/Xorg 21.1.24 with COMPOSITE on/off. Sources, archives and logs
are under `build/desktop/ps2-transparent-stipple`; see
[component evidence](../../docs/graphics-transparent-validation.json).
Build/check/export/record helpers are `build-ps2-transparent.py`,
`check-ps2-transparent-host.py`, `export-ps2-transparent-patches.py` and
`record-ps2-transparent-progress.py` under `build/verification`.

The distinct `ps2-gs-transparent-test` adds six host/R5900 lifecycle checks,
preserves seventeen earlier binaries and uses the stable 576p kernel.
Build/check/package/record helpers are `build-ps2-transparent-probe.py`,
`check-ps2-transparent-probe-host.py`, `package-ps2-gs-transparent.py` and
`record-ps2-transparent-runtime.py`. PCSX2 visual, physical and full
DDX/GLX/TWM qualification remain separate; earlier evidence is unchanged.

PCSX2 completes all four transparent-stipple command/fence scenes with exit zero
and text-mode restoration at 70.664 guest seconds. See
[text-only runtime evidence](../../docs/graphics-transparent-runtime-validation.json). All four supplied panels match the expected scenes, and the user confirms all
four and console return. This qualitative verification stays text-only;
physical/RGB555 pixels and full X server/GLX/TWM use remain unqualified.

## Opaque stipple Boolean ALUs and partial RGB masks

Apply `mesa-ps2-opaque-stipple-logic.patch` after
`mesa-ps2-transparent-stipple.patch` and `xorg-ps2-opaque-stipple-logic.patch`
after `xorg-ps2-transparent-stipple.patch`. All 16 ALUs and RGB masks use
disjoint zero/one runs, applying one background/foreground operation per pixel.
Equal masked colours collapse each box, and optimized opaque copy repeat remains.
The shared snapshot/preflight/run limits bound work without CPU colour fallback
or new OpenGL capabilities. Both full generic series apply cleanly and changed
components compile against pinned Mesa 24.0.9/Xorg 21.1.24 with COMPOSITE on/off.

Sixteen new plus 114 regressions pass per environment. Exact sources, archives
and logs are under `build/desktop/ps2-opaque-stipple-logic`; see
[component evidence](../../docs/graphics-opaque-stipple-logic-validation.json).
Build/check/export/record helpers are `build-ps2-opaque-logic.py`,
`check-ps2-opaque-logic-host.py`, `export-ps2-opaque-logic-patches.py` and
`record-ps2-opaque-logic-progress.py` under `build/verification`.

`ps2-gs-opaque-test` adds six host/R5900 probe lifecycle checks and preserves
eighteen preceding binaries with the stable kernel. Build/check/package/record
helpers are `build-ps2-opaque-probe.py`, `check-ps2-opaque-probe-host.py`,
`package-ps2-gs-opaque.py` and `record-ps2-opaque-runtime.py`. PCSX2 visual,
physical/RGB555 and full DDX/GLX/TWM qualification remain separate.

PCSX2 completes all four opaque Boolean command/fence scenes with exit zero
and text-mode restoration at 109.3742 guest seconds. See
[text-only runtime evidence](../../docs/graphics-opaque-stipple-logic-runtime-validation.json). All four opaque Boolean panel scenes match; user console confirmation is pending;
physical/RGB555 pixels and full X server/GLX/TWM use remain unqualified.


### Thin solid lines

Apply `mesa-ps2-thin-lines.patch` after `mesa-ps2-opaque-stipple-logic.patch` and
`xorg-ps2-thin-lines.patch` after `xorg-ps2-opaque-stipple-logic.patch`.
The shared geometry adapter preserves Xorg zero-line bias, original clipped
phase, caps and joins, then the native GS solid batch applies Boolean colour
operations. No CPU colour destination renderer or new GL capability is added.
Clean series apply/source equality and pinned-release compilation pass with
COMPOSITE enabled and disabled. See
[component evidence](../../docs/graphics-thin-lines-validation.json).

Qualified thin-solid-line archives/source snapshots are under `build/desktop/ps2-thin-lines`.
The isolated `ps2-gs-line-test` image preserves nineteen earlier diagnostics;
142 component and six probe lifecycle checks pass per host/R5900 environment.
Wide/dashed lines/arcs, actual X server/Damage, complete DDX/Mesa/GLX/TWM and
physical/RGB555 pixels and desktop RAM remain unfinished. Earlier patches and
qualified source snapshots remain unchanged.

PCSX2 completes all four thin-line command/fence scenes with exit zero and
text-mode restoration at 70.0678 guest seconds. See
[text-only runtime evidence](../../docs/graphics-thin-lines-runtime-validation.json).
The exact binary/image/session and completion log are preserved. All four
supplied line panels match, and the user explicitly confirms console return.
No images are copied or linked in the documentation. Physical/RGB555 pixels,
actual server/Damage, performance and full desktop use remain separate.


Apply `mesa-ps2-thin-dashes.patch` after `mesa-ps2-thin-lines.patch` and
`xorg-ps2-thin-dashes.patch` after `xorg-ps2-thin-lines.patch`. The shared dash
adapter preserves original phase through clipping, continues it across
polyline joins and resets independent segments/rectangles. On/off gaps preserve
the destination; double dash uses disjoint foreground/background native GS
Boolean runs. The GC requires positive even normalized lists and FillSolid.
All sixteen ALUs and RGB masks remain supported. Complete coloured-run
preflight occurs before the existing queue fence. No CPU colour renderer or
new GL capability is added. Clean series application, source equality and
pinned-release compilation pass with COMPOSITE enabled and disabled; see
[component evidence](../../docs/graphics-thin-dashes-validation.json).

Current archives/source snapshots are under `build/desktop/ps2-thin-dashes`.
The distinct `ps2-gs-dash-test` image preserves twenty earlier diagnostics;
158 component and six probe lifecycle checks pass per host/R5900 environment,
plus 39 host lifecycle checks of earlier probes. PCSX2 completes all four
new scenes with exit zero and text-mode restoration at 69.5018 guest seconds;
see [text-only runtime evidence](../../docs/graphics-thin-dashes-runtime-validation.json).
All four supplied scene panels match, and the user explicitly confirms console return. Wide lines/arcs, patterned dash fills,
actual X server/Damage, complete DDX/Mesa/GLX/TWM, physical/RGB555 pixels and
desktop RAM remain unfinished. Earlier patches, source snapshots and saved
binaries/images/logs remain unchanged.


### Native monochrome cursor

Apply `mesa-ps2-native-cursor.patch` after `mesa-ps2-thin-dashes.patch` and
`xorg-ps2-native-cursor.patch` after `xorg-ps2-thin-dashes.patch`. The Mesa patch
adds `ps2_cursor.c/h`; Xorg adds `ps2_cursor_screen.c/h` and consumes the public
Mesa cursor API. Both products receive the shared transport front-access
observer changes, so reads/copies as well as writes remove a visible cursor.
Install the Xorg screen-owned cursor after native GC and Damage setup, using a
CPU-unmapped front pixmap and one pointer device. Keep the copied screen
functions and owning records alive until CloseScreen. Suspend before leaving
the VT; resume after the DDX restores the front and GS ownership. Those actual
DDX VT/input hooks remain pending. ARGB and cursors larger than 64x64 reject.

GS copies capture and restore a 64x64 saved background; native Boolean runs
draw the snapshotted monochrome mask/source. No CPU colour destination renderer,
readback or new GL capability is added. Clean series application, source
equality and pinned-release compilation pass in both COMPOSITE modes; see
[component evidence](../../docs/graphics-native-cursor-validation.json).

Current archives/source snapshots are under `build/desktop/ps2-native-cursor`.
174 component and six probe checks pass per host/R5900 environment, plus 45
host lifecycle checks of earlier probes. A focused repeat of the sixteen
cursor host cases validates failed-restore cleanup. The distinct isolated
cursor image preserves 21 earlier diagnostics. MI pointer/Damage framework
and screen bookkeeping are stubbed; actual X server, input/VT, complete DDX/
Mesa/GLX/TWM, physical/RGB555 pixels and desktop RAM remain unqualified.


PCSX2 completes all four cursor scenes with exit zero and text-mode restoration
at 168.4982 guest seconds; see [text-only runtime evidence](../../docs/graphics-native-cursor-runtime-validation.json).
All four supplied panels match qualitatively: the small white/black pixelated
arrow is intended, movement leaves no visible trail, the front copy excludes
cursor pixels and the final panels are clean. Explicit user console-return
confirmation is pending. Positions are scripted; USB mouse input and actual
X server/Damage/VT execution are not qualified by this diagnostic.


### Native colour client images

Apply `mesa-ps2-client-images.patch` after `mesa-ps2-native-cursor.patch` and
`xorg-ps2-client-images.patch` after `xorg-ps2-native-cursor.patch`. Mesa adds
`ps2_image.c/h`. Xorg adds `ps2_image_gc.c/h`, the public Mesa image header and
native PutImage/SetSpans registration in the existing GC registry. Install the
image owner after native GC setup, before outer cursor hooks. It retains a
context lease, restores image dispatch and its CloseScreen wrapper before
lower GC/context teardown. Depth-1 source editing keeps its lower path.

Native ZPixmap sources require matching RGBX/CT32 or RGB555/CT16, zero left pad
and positive padded rows. The planar-image patch below adds XY formats for
colour destinations. PutImage remains drawable-relative regardless of miTranslate;
SetSpans points are already translated. All sixteen Boolean ALUs and partial
RGB masks use bounded native colour runs; full-copy/full-mask images use GS
uploads. Source/box snapshots and all work/allocation validation precede the
original queue fence. No destination mapping/readback or CPU colour rendering
is added. Clean series application, exact equality and pinned compilation pass
with COMPOSITE enabled and disabled; see
[component evidence](../../docs/graphics-client-images-validation.json).

Archives and source snapshots are under `build/desktop/ps2-client-images`.
190 component and six probe checks pass per host/R5900 environment, plus 51
host lifecycle checks of older probes. The separate image diagnostic preserves
22 prior binaries and the unchanged kernel. MI pointer/Damage framework and
screen bookkeeping remain stubbed; full Xorg/input/VT, Mesa/GLX/TWM, physical/
RGB555 pixels and measured desktop RAM remain separate qualifications.

The isolated new diagnostic also completes all four PCSX2 command/fence scenes
with exit zero and text-mode restoration at 163.0047 guest seconds in 32 MiB;
see [text-only runtime evidence](../../docs/graphics-client-images-runtime-validation.json).
All four supplied panels match qualitatively; the user confirms all scenes
and normal text-console return.
Real server/Damage/VT, physical/RGB555 pixels and a complete desktop remain
separate qualifications.

### Native bitmap and bitplane images

Apply `mesa-ps2-planar-images.patch` after `mesa-ps2-client-images.patch` and
`xorg-ps2-planar-images.patch` after `xorg-ps2-client-images.patch`. Mesa adds
`ps2_planar.c/h`; Xorg extends the image GC callback and copies the minimal
public planar header. Existing image owner/context leases, revalidation and
reverse cleanup serve all image formats; install before outer cursor hooks.
XYBitmap maps depth-1 source bits to foreground/background; XYPixmap requires
native destination depth 15/24 and descending significance. Padded rows and
left pads 0-31 follow pinned server contracts. Full RGB-mask GXcopy uses native
GS colour fills; other ALUs/masks use Boolean runs that preserve spare bits.
Full-copy spare bits are unspecified. All paths retain source snapshots and work preflight before
the original fence. ZPixmap retains its existing upload/Boolean paths.

No colour destination mapping/readback or colour-pixel expansion array is
added. Source bytes are limited to 2 MiB, clipped plans to 4096 boxes and XY
work to 16384 runs. The driver allocation bound is 2621440 bytes, excluding
the original client buffer and allocator overhead. Archives/snapshots are in
`build/desktop/ps2-planar-images`; clean patch/equality/pinned-release compilation
pass in both COMPOSITE modes. See [component evidence](../../docs/graphics-planar-images-validation.json).
206 component and six diagnostic checks pass per host/R5900, plus 57 earlier
host probe lifecycle checks. The isolated diagnostic preserves 23 binaries and
the unchanged kernel. MI/Damage remain minimal scaffolding; real server/input/
VT, physical/RGB555, Mesa/GLX/TWM and total desktop RAM remain unqualified.

The [initial visual run](../../docs/graphics-planar-images-initial-visual-failure.json)
failed the backing zigzag panel. Its artifacts remain frozen. The corrected
full-copy path completes PCSX2 with exit zero at 176.5307 guest seconds in
32 MiB; the user confirms all four scenes and console return, and the final
front/backing zigzags match qualitatively. See
[corrected runtime evidence](../../docs/graphics-planar-images-runtime-validation.json).


### Native solid FillPolygon

Apply `mesa-ps2-filled-polygons.patch` after `mesa-ps2-planar-images.patch`
and `xorg-ps2-filled-polygons.patch` after `xorg-ps2-planar-images.patch`.
Mesa adds `ps2_polygon.c/h`; Xorg adds `ps2_polygon_gc.c/h`, the public polygon
header and optional FillPolygon registration in the GC owner. Install before
outer cursor hooks and clean up before the GC/context owner. Native colour
hooks survive GC revalidation; depth-1 editing retains lower dispatch.

Both fill rules and coordinate modes scan-convert bounded solid geometry
before fencing. Full RGB-mask GXcopy uses native GS fills with unspecified
spare bits; other ALUs/masks use Boolean runs that preserve those bits.
No colour destination mapping, pixel readback or colour expansion buffer is
added. Limits are 4096 vertices, 256 clips, 4194304 edge tests and 16384 runs.
In-place sort keeps request driver heap at most 425984 bytes, excluding the
original client buffer and allocator overhead; Mesa/Xorg frames are 112/4216.

Archives and source snapshots are in `build/desktop/ps2-filled-polygons`.
Clean series application/equality and pinned compilation pass in both
COMPOSITE modes. Eighteen new plus 206 regressions pass per host/R5900 against
independent pinned MI pixels; see
[component evidence](../../docs/graphics-filled-polygons-validation.json).
Six separate diagnostic pixel/lifecycle checks pass per environment; the new
image preserves 24 preceding binaries and the stable kernel. Solid polygons
are supported; polygon tiles/stipples, real DDX/input/VT/Damage, physical/RGB555,
complete Mesa/GLX/TWM and total desktop RAM remain separate qualifications.

The polygon probe exits zero at 229.5788 guest seconds in 32 MiB PCSX2.
The four supplied scenes pass qualitative review and the user confirms
text-console return; see
[runtime evidence](../../docs/graphics-filled-polygons-runtime-validation.json).
All 63 earlier host probe checks pass against the updated components;
see [legacy probe evidence](../../docs/graphics-filled-polygons-legacy-probe-validation.json).

### Monochrome stippled FillPolygon

Apply `mesa-ps2-stippled-polygons.patch` and `xorg-ps2-stippled-polygons.patch`
after filled polygons. Mesa exposes a geometry-only spans plan and adds the
stippled polygon adapter; Xorg snapshots server or owned monochrome sources
through the existing polygon owner. Both fill rules/coordinate modes and
drawable-relative pattern origins are preserved. Transparent zero bits leave
destination unchanged; opaque runs apply one foreground/background operation.
All ALUs/RGB masks use native colour fills or Boolean runs without CPU colour
destination rendering. Source/pixel/run budgets precede fencing; driver heap
is bounded by 696320 bytes, excluding original client storage and allocator
overhead. Both COMPOSITE modes pass clean application/equality and pinned compile.

All 242 component checks and six diagnostic pixel/lifecycle checks pass per
host/R5900, plus 69 earlier host probe checks; see
[component evidence](../../docs/graphics-stippled-polygons-validation.json) and
[legacy evidence](../../docs/graphics-stippled-polygons-legacy-probe-validation.json).
New archives/snapshots are isolated in `build/desktop/ps2-stippled-polygons`.
The distinct diagnostic image preserves 25 preceding binaries and the stable
kernel. The new probe exits zero at 174.9583 guest seconds in 32 MiB PCSX2;
all four supplied scenes pass qualitative review and the user confirms normal
text-console return. See
[runtime evidence](../../docs/graphics-stippled-polygons-runtime-validation.json).
Real DDX/GLX and
physical/RGB555 pixels remain separate qualifications.

### Transformed 3D geometry component

Apply `mesa-ps2-transformed-triangles.patch` after `mesa-ps2-stippled-polygons.patch`.
It adds `ps2_transform.c/h` with a bounded row-major MVP, seven homogeneous
clip planes and viewport/depth conversion to native GS opaque triangles/Z16.
R5900 scalar geometry and affine GS Gouraud colours are explicit limits;
shader caps remain zero, and `draw_vbo`, GLX and VU programs remain unfinished.
The complete Mesa component series applies cleanly to the pinned release and
compiles in both COMPOSITE modes. No Xorg patch is added: the diagnostic uses
the existing shared-context and native EXA Copy callbacks. All 261 component
checks and six diagnostic checks pass per host/R5900 environment; see
[component evidence](../../docs/graphics-transformed-triangles-validation.json).
New archives/snapshots are isolated in `build/desktop/ps2-transformed-triangles`.
The separate `ps2-gs-transform-test` image preserves 26 preceding binaries,
18 earlier images and qualified 2D reports.

The new probe exits zero at 209.9709 guest seconds in 32 MiB PCSX2. All four
still scenes pass qualitative review, with user-confirmed text-console return;
see [runtime evidence](../../docs/graphics-transformed-triangles-runtime-validation.json).
Continuous animation is not part of this fixed-angle diagnostic.

### GS-only Boolean copies, tiles and native client readback

Apply `mesa-ps2-gpu2d.patch` after transformed triangles and
`xorg-ps2-gpu2d.patch` after stippled polygons. These synchronize the current
shared components, add the GS-only Boolean copy/tile engine, and add native
colour GetImage/GetSpans. Both patches apply to their complete preserved
component series and their changed C sources compile with and without
COMPOSITE against the pinned Mesa/Xorg headers. The Xorg patch includes the
shared Mesa component sources needed by the standalone DDX. The production
module is built by `scripts/native_xorg.py`; patches alone do not enable GLX.

The kernel requires `ps2-gs-transfer.patch` followed by
`ps2-gs-readback.patch` after the existing logic patch. CPU geometry planning,
source upload and serialization of read-only client replies remain; colour
destination rendering and plane masking execute on GS. Generic copies and
tile fills support all sixteen Boolean ALUs and partial RGB masks in native
RGBX/RGB555 formats. The workspace occupies at most 56 KiB of GS VRAM. Native
readback drains at most 8 KiB per kernel request and restores bus direction
on success or timeout. The DDX installs readback after EXA initialization,
retaining EXA's source-bitmap access handling. It also installs opaque
XRender source copies. Colour-source CopyPlane, alpha/masked/transformed
XRender, complete GLX/OpenGL and physical qualification remain unfinished.

Current artifacts and evidence are isolated under
`build/desktop/ps2-gpu-2d`; older qualified images and patch reports remain
available. The new actual Xlib client compares returned GS pixels with an
independent client-side oracle before running the existing TWM/xterm session.

The current GPU2D patches also restore cursor save-under pixels using two GS
textured triangles. Saving remains a native local transfer; restoring requires
the existing native texture capability and does not read colour destinations
into CPU memory. This removes a stale cursor-sized black rectangle in PCSX2's
hardware presentation. The Linux launcher separately disables PCSX2's clear
shortcuts to remove striped backgrounds/title bars while retaining OpenGL
hardware rendering. Both changes are confirmed visually in revision `r9`.

The current patches additionally use `PS2GS_SOLID_BATCH` when advertised by
the kernel, retaining the older solid-logic path otherwise. Up to 64 ordered
metadata rectangles share an ioctl; kernel `ps2-gs-batch.patch` validates the
snapshot and joins groups of eight GS operations into one bounded DMA packet.
The render/texture endpoints also group their existing validated requests.
Every group waits once for FINISH, preserving colour masks, Boolean ALUs,
depth ordering and alpha semantics without destination pixel staging. Region
cursor guards avoid hiding on unrelated damage, and client readback restores
the cursor before protocol replies are sent. The r13 native qualification
passes 141 pixel comparisons and the complete interactive session; see
[batch validation](../../docs/graphics-gpu2d-batch-validation.json).
