# PS2 hardware graphics

The native Xorg driver and shared Mesa 2D backend render through PS2 GS
commands. CPU framebuffer raster access is rejected. Mesa OpenGL/GLX and existing
OpenGL application support remain unfinished.

Hardware-only `startx` requires `/usr/lib/xorg/modules/drivers/ps2_drv.so` and
stops while that driver is absent. The current core-X hardware test candidate
includes the native module. No software Mesa renderer is installed.
The fbdev package in the diagnostic build is not the selected runtime drawing
driver. `startx-ps2` is a compatibility alias for classic `startx`.

## Current PS2 GS hardware 2D

Revision r43 makes TWM Icon Manager entries match the window titles: `Clock`
and `Emotion Engine | Terminal` replace the applications' icon names. Entries
update and re-sort on title changes, including while minimized. The aligned
186-pixel panel, anchored growth and click-to-restore checks pass with GS-rendered
label readback. `xeyes`, `uxterm` and bitmap draw/save regression checks pass too.
The [r43 validation](../build/graphics-applications-validation-20261018-r43/final-validation.json)
records this scope and retains the prior r42 application coverage. The r42 image
is preserved. Normal `make xorg-apps` builds apply the TWM change automatically.

Revision r42 fixes bitmap's complex-region launch crash and shaped-window
image-reply crashes while retaining GS-only rendering and the r41 mouse fix.
All 74 installed Xorg programs passed native functional checks across documented
32 MiB emulator sessions. These checks cover GUI launch/readback/resize/expose
and command/session functions; they do not exhaust every application feature.

US/ANSI is now the default, with backslash/pipe above Enter. `loadkeys` and
`ps2-keyboard` provide 21 selectable console/Xorg presets. The full Clock title
is visible, and the bottom-right window list shows open/minimized applications.
Its title and rows share a compact 186-pixel width measured from the title font,
all four buttons and padding. Minimize, click-to-restore and anchored growth pass.

Matching GNU libc UTF-8 data and Xlib locale definitions fix uxterm's locale
warning. The login profile keeps the lightweight `C` default; native libc, Xlib and uxterm
checks pass. Normal Make targets include the driver, patches, layouts and locales.
Repeated wallpaper refreshes reuse one GS pixmap rather than leaking VRAM.
MIT-SCREEN-SAVER is enabled for `beforelight`; session utilities are checked too.

The GNU library cache preserves the selected libraries for 85 executables while
reducing failed path lookups from 1,534 to 119. Xorg uses nice -5. The recorded
short CPU-load pointer comparison supports that scheduling choice; physical
timing is unmeasured. The GNU tools and all 70 Xorg package selections remain.

Use the [r43 2 GB USB image](../build/eelinux-images/eelinux-pal-desktop-gs2d-r43-20261018-2gb-usb.img),
[checksum](../build/eelinux-images/eelinux-pal-desktop-gs2d-r43-20261018-2gb-usb.img.sha256),
[native validation](../build/graphics-applications-validation-20261018-r43/final-validation.json)
and [desktop capture](../build/graphics-applications-validation-20261018-r43/native-v2/ps2-desktop-qa/desktop.png).
Normal login and `startx` remain; no long graphics tests run automatically.
The original working image, r40 and r41 are unchanged.

Driver sampling, conversion, filtering, blending and coverage use PS2 GS commands.
CPU/software raster fallback remains rejected; protocol, clipping geometry,
constants, command submission and raw byte transfers still use the CPU. Core
X11, 38 Direct XRender formats, 53 operator IDs and six filter names remain
advertised within documented device/request limits. Mesa OpenGL/GLX and
exhaustive physical rendering/performance qualification remain unfinished.

The prior r41 focused checks and physical responsiveness report remain in its
[revision record](../build/graphics-hardware-2d-validation-20261018-r41/revision-record.json).
Its separate 24,905-check/39-session graphics run is tracked through
[live progress](../build/graphics-hardware-2d-full-qualification-20261018-r41/progress.json);
that earlier driver run does not certify r42 or r43.

Window source/mask transforms are translated exactly into backing-pixmap
coordinates, including redirected backing offsets. Sampling and repeat execute
on GS; coordinate overflow is rejected before drawing.

The complete archive workload expects 24,905 Xorg checks across 39 sessions,
plus the GS primitive, arithmetic and cursor probes. Packaging-time metadata
describes a console candidate; runtime records distinguish the workloads actually
executed. Large matrices and full-display exact blends can take a long time under
CPU GS emulation. Ordinary `startx` runs the desktop without those diagnostics.

The ready profile uses the matching kernel, complete image, local BIOS selection,
CPU GS Renderer13 and no automatic timeout:

```powershell
py -3 scripts/emulator.py launch --directory build/emulator/pcsx2-gs2d-r43-20261018 --host windows --pcsx2 "C:\Program Files\PCSX2\pcsx2-qt.exe"
```

Log in as `root` with password `eelinux`, then run `startx`.

Render trapezoids, triangles, strips, fans and direct traps generate A1/A4/A8
coverage with GS sample counting and packing. CPU fixed-point edge stepping emits
sample-space geometry. Direct AddTraps follows picture storage bpp. Combined
masks, per-shape masks, source anchors, clipping and zero-mask operator extents
follow the pinned Xorg/Pixman behavior. Destination transform, repeat and filter
state is ignored while the original source sampling state is retained, as in the
[Render protocol](https://www.x.org/releases/current/doc/renderproto/renderproto.txt).
The driver snapshots aliased inputs; an in-place Pixman iterator can observe its
own earlier stores and is not used as the frozen-input oracle.

Geometric coverage is bounded to 8,192 trapezoids or 4,096 triangles per request,
16 Mi sample-work units, target dimensions up to 2,048, and the native raw-pixmap
storage budget. Integer rectangles use the established GS compositor directly.
GS VRAM is 4 MiB, handles are bounded to 128, and cursor images are limited to
64x64. Unsupported or exhausted rendering has no software raster fallback.

## Earlier console candidates and development history

The remaining-work statements below describe the recorded historical revisions.


The user requires the PlayStation 2 Graphics Synthesizer as the rendering target.
No NVIDIA/D3D12 or software raster renderer is part of the guest driver or console
package. Emulator results do not establish physical PS2 hardware correctness.

[The console test archive](../build/graphics-hardware-2d-console-20261017-r33-v1/ps2-gs-console-test.tar.gz)
contains the matching compressed kernel, production DDX, GS primitive diagnostic,
expanded 19,307-check Xorg client, and the production exact RGB multiplication probe.
Follow [the console instructions](../build/graphics-hardware-2d-console-20261017-r33-v1/ps2-gs-console-test/README.txt).
The r33 candidate also includes 126 focused nearest Saturate Xorg checks, 56
packed, 12 bilinear, 24 coverage, 24 solid, 48 alpha-map Saturate, 12 shifted-map,
12 convolution, 32 byte/word alias, 24 native depth-15 alias, 198 Screen, 198 Exclusion, 198 Multiply, 594 Darken/Lighten/Difference, 396 Overlay/HardLight, 1,412 disjoint/conjoint aliases/OverReverse, 104 ratio/regression, 30 ColorDodge/ColorBurn, 20 SoftLight, 66 HSL, 72 gradient, 72 ten-bit, 68 ten-bit operator, 64 ten-bit convolution, 32 ten-bit alpha-map read and 64 writable ten-bit alpha-map image checks,
and the 32,768-channel GS Saturate and 626,688-channel ordered-blend probes. The r7 packed
matrix and the broader 126-check nearest matrix pass under PCSX2 CPU GS emulation.
Its default runner also requires 24 window and 114 packed-map alias checks,
for 23,529 Xorg checks across thirty sessions, plus 24,576 GS integer division
comparisons. Focused modes are available for each extension.
The test loads its DDX from the extracted directory and saves logs there; it does
not replace the installed driver. Boot the supplied `EELINUX-GS.ELF` with the
existing GNU/Xorg root on USB partition 2. Physical execution is pending.

The candidate supports both channel orders of RGB555/RGBX/ARGB, BGRA/BGRX
and depth-32 RGBX, with nearest, bilinear and signed convolution, transforms,
repeat modes, aliases, thirteen basic operators, Screen, Exclusion, Multiply, Darken, Lighten, Difference, Overlay, HardLight, ColorDodge, ColorBurn, SoftLight, all four HSL modes and all disjoint/conjoint operators. Component-alpha colour masks
sample RGB and alpha separately on GS. RGB444 in both orders and the four
RGB332/BGR233/ARGB2222/ABGR2222 byte layouts also decode and encode on GS.
Depth-16 RGB565, ARGB1555, ARGB4444, RGB555 and RGB444 in both orders use
unchanged raw client words uploaded to packed CT32 storage. GS lookups expand
the channel fields, the GS compositor renders them, and GS lookups quantize and
pack the result. All 65,536 words per layout pass the independent pixman codec
oracle in both development environments. Unused RGB555/RGB444 bits and row
padding are preserved by the codecs. Aliased Render stores zero observable
padding fields as described below. No CPU colour conversion is used in the driver.
A4 coverage uses the low nibble of native byte storage, with GS core drawing,
uploads, replies, sampling, filtering, blending and encoding. Component-alpha A1/A4/A8 masks use zero RGB
components and independently filtered coverage on GS. Exact multiplication retains sixteen-bit
products with GS nibble products and carry planes, then rounds once by 255.
Both development environments pass 196,608 channel comparisons and 393,216
unfiltered coverage Saturate triples at six destination-alpha boundaries.

Packed A1/A4/A8 bytes and depth-16 core words upload unchanged. GS lookups
decode them into pixel planes; GS samples, filters, blends and encodes the
result. Core A1/A4/A8 and depth-16 fills,
copies, text and native geometry use the same GS Boolean drawing paths.
Planar/packed uploads and colour-source CopyPlane pixel selection execute
on GS. A4/A8/16-bit-to-colour CopyPlane also decodes source bytes and selects
foreground/background on GS.
GetImage/GetSpans mask pixels on GS before protocol serialization. CPU work
includes geometry planning, constant lookup tables and raw client transfers.
Owned raw protocol buffers remain in main RAM; rendering runs on GS canvases.
`PrepareAccess` rejects CPU raster access for every pixmap depth.

The production module builds with `-Wall -Wextra -Werror`; exported Mesa/Xorg
sources match it and compile with COMPOSITE enabled and disabled. The expanded
client's independent oracle passes 16,940 available-format checks on Xvfb at
depths 15 and 24. Stock Xvfb lacks A4 and the new byte-colour formats; 2,367
Render checks per depth are therefore skipped explicitly in host oracle mode.
The native client requires those formats and expects all 19,307 checks. Across the retained
runs, 239 distinct model cases pass in each development environment. Raw CopyPlane
passes every A4/A8/16-bit source bit, all sixteen Boolean operations and full/partial
masks for RGB555, RGBX and raw32 destinations. See
[the development record](graphics-hardware-2d-validation.json) for hashes and
model checks. These results do not qualify the physical GS.

The focused native Xorg alpha-map suite passes all 981 pixel checks and exits
with client/server status zero. A1/A4/A8 maps replace source and mask alpha
before filtering; destination maps provide alpha and receive GS-computed
results. The direct Render hook preserves map clipping and bypasses EXA paths
that would otherwise enter software. The run includes thirteen basic operators,
nearest/bilinear/convolution, component alpha, nonzero origins and unmasked
identity requests. The full 19,307-check native suite has not completed at this
revision. Broader window, raw-storage alias and colour-map qualification remains
open. Immutable sources and logs are retained in
[the alpha-map revision record](../build/graphics-hardware-2d-validation-20261014-r4/revision-record.json).
PCSX2 emulated GS packets on the CPU; this is not physical hardware proof.

The r5 driver also passes 24 native drawable-relative window-map checks and
114 native packed-map alias checks. Destination-map clipping uses the parent
window origin, including backing-pixmap offsets. Coincident same-format aliases
share one decoded GS canvas across A1/A4/A8 and fourteen byte/word layouts;
At r5, A8 also supports shifted map origins; other shifted packed aliases
and differing converted-format aliases fail before rendering. The r12 extension
below adds shifted same-format aliases through GS per-store quantization. The separate GS u32/u24 division primitive passes
24,576 native channel checks and retains all operands in VRAM; it does not yet
enable additional XRender operators. See
[the r5 record](../build/graphics-hardware-2d-validation-20261014-r5/revision-record.json).
Stock Xvfb fails the drawable-relative window-map comparison at depths 15 and
24; that comparison is retained separately from the passing direct-pixman and
native GS checks.

The wider arithmetic extension adds exact 24x24-to-48-bit products and binary32
multiplication, division, nonnegative addition and subtraction with nearest/even
rounding. Each floating operation passes 24,576 native results under PCSX2 CPU
GS emulation; the integer product passes 147,456 byte comparisons. Host and
R5900 checks also compare UNORM normalization and packing against pixman in
six layouts. Operands stay in GS VRAM; the CPU prepares constant tables and
geometry. The arithmetic contracts exclude general signed or subnormal math.

The new nearest UNORM8 Saturate compositor passes 8,192 native RGBA pixels,
including scalar and component-alpha masks, zero alpha and clamping. Its Xorg
dispatch passes host and R5900 models. The r6 native Xorg matrix found an
opaque RGBX source rounding mismatch. The r7 compositor matches pixman's
opaque-source reduction to OverReverse using GS byte arithmetic. Its new
56-check native packed-format matrix passes with zero errors and successful
Xorg shutdown; the broader 126-check nearest matrix also passes. Those two
matrices total 182 native Xorg checks for the frozen r7 module.
Original UNORM1..8 precision now reaches the GS binary32 path before decoded
colour expansion can lose it. Four byte and ten word colour layouts support
nearest Saturate in both conversion directions with component-alpha masks.
The kernel's handle table grows from 64 to 128, retaining the 4 MiB VRAM bound,
reserved regions and validated allocations. The development source adds wide
bilinear sampling with all 16 fraction bits, original UNORM precision and
GS binary32 blending. Its primitive passes 16,128 native byte comparisons and
its integrated Xorg path passes 12 focused native checks, including separate
source/mask filtering, channel orders, component alpha and a source alias.
The frozen r8 host regression passes 267 cases across 15 suites; that package includes the
focused bilinear mode. See [the r8 evidence](../build/graphics-hardware-2d-validation-20261014-r8/revision-record.json).
The r9 extension adds A1/A4/A8 Saturate destinations with bilinear source/mask
filtering, component-alpha masks and source/mask aliases. All 24 focused native
Xorg checks pass with PCSX2 CPU GS emulation and clean Xorg shutdown. Its alpha
result uses GS binary32 multiply/add/clamp and original UNORM quantization,
avoiding the RGB factor and division work. Independent host and R5900 models
compare 24,576 alpha pixels per environment with pixman, including rounding
boundaries, A1/A4/A8 quantization and untouched pixels. The diagnostic interval
falls from 544.9304 to 209.9817 emulated seconds for the same 24 checks, including
Xorg startup/shutdown; this is not physical PS2 or wall-clock performance.
The r9 full host regression passes 274 cases across 15 suites, and four focused
R5900 cases pass.
See [the r9 evidence](../build/graphics-hardware-2d-validation-20261015-r9/revision-record.json).
The r10 extension retains original RGBA16 solid-source and solid-mask colours
for Saturate, including scalar and component-alpha masks. Alpha 65534 remains
translucent even when its cached eight-bit alpha is 255. Actual opaque-source
operator reduction retains the pinned pixman behavior. Uniform filter and
transform attributes are ignored for solids, matching their constant sampling.
CPU code normalizes uniform constants; GS performs sample-dependent arithmetic
and all destination packing. Native/packed colour and A1/A4/A8 targets pass
24 focused native Xorg checks with clean shutdown. Independent host and R5900
models each pass 960 scenarios across three native, fourteen packed and three
coverage formats, plus eleven metadata guards. The merged `.c` diagnostic builds
to the same binary used for native qualification.
The full host regression passes 278 cases across 15 suites; the four new cases
pass again on host and R5900 after the test helpers were merged into `.c`.
Production C/H is unchanged by that reorganization. See
[the r10 evidence](../build/graphics-hardware-2d-validation-20261015-r10/revision-record.json).
The r11 extension adds source/mask alpha replacement before wide nearest and
bilinear Saturate sampling. Original map precision is retained, including A1,
A4 and packed colour alpha. Separate GS quantization writes parent and map
RGB/alpha from the same float result, preserving map precision even when it
differs from the parent. Native/packed colour and coverage targets, alpha-only
and colour writable maps, scalar/component-alpha masks and input-map aliases
pass 592 independent model scenarios per environment. All 48 native Xorg
comparisons over 24 Render requests pass with client/server status zero.
At r11, shifted writable Saturate map aliases remain rejected before drawing.
At r11, wide convolution and more-than-eight-bit image channels still need
Saturate integration. A separate 14-case conversion probe finds eight host/R5900
differences in the pinned wide-convolution accumulation. The R5900 soft-float
runtime clamps negative float-to-u32 to zero and positive overflow to UINT32_MAX;
the r13 GS implementation below reproduces those target conversions.
The r11 full host regression passes 287 cases across 15 suites. Nine focused
R5900 cases pass, including 26 new primitive guards. Sources, binaries and logs
are retained in [the r11 evidence](../build/graphics-hardware-2d-validation-20261015-r11/revision-record.json).
The r12 extension supports shifted same-format writable alpha-map aliases.
GS buffers retain the current destination-map scanline before drawing,
quantize parent stores at their original packed precision, and publish the
map row after the complete parent row. Subsequent rows observe those stores.
This matches the pinned pixman destination iterator without CPU pixel work.
All 12 native Xorg checks pass, covering eight native/packed/coverage Saturate
and four Over requests with positive/negative map origins and clean shutdown.
Host and R5900 models each pass 1,610 new scenarios: 1,326 basic-operator
packed cases, 176 Saturate cases across 22 layouts, and 108 native scanline
boundary/input-alias cases at width 71. Native focused rectangles are smaller.
The full host regression passes 292 cases across 15 suites; seven targeted
R5900 cases pass, including the updated unsupported-alias guard. Differing
decoded-format writable aliases, broad native regression, physical memory
and performance qualification remain open. See
[the r12 evidence](../build/graphics-hardware-2d-validation-20261015-r12/revision-record.json).
The r13 extension adds wide signed convolution Saturate. Each GS accumulator
holds the rounded unsigned value scaled by 2^-32. GS addition/subtraction,
per-tap integer truncation and overflow clamping preserve the pinned R5900
pixman behavior. The final GS stage applies signed-int32 / 65536 and the
[0,1] clamp. Coefficient/UNORM lookup tables contain uniform constants;
the CPU never reads sampled pixels to compute a filtered result.
Source/mask convolutions retain transforms, repeats, original alpha-map
precision, component alpha, packed targets and writable maps.
All 12 native Xorg comparisons pass with clean client/server shutdown.
The final client varies native destination alpha and includes an ARGB4444
overflow/cancellation kernel that filters opaque white to one; unsigned
wrapping would produce zero. This distinguishes the R5900 conversion on GS.
Host and R5900 models each verify 36,864 arithmetic float components, 13
guards and 180 integration scenarios, including 22 targets and three map
precisions. Portable integration kernels stay positive; explicit target-cast
oracles and native R5900 pixman cover signed/overflow behavior. Six legacy
coverage cases now execute convolution rather than expecting rejection.
The full host regression passes 299 cases across 15 suites. Thirteen
targeted R5900 cases pass. The full native primitive/format matrices and
physical qualification remain pending. See
[the r13 evidence](../build/graphics-hardware-2d-validation-20261015-r13/revision-record.json).

The r14 extension synchronizes different byte/word-format writable maps
through GS raw backing, preserving scanline store order and observable padding.
Its 32 focused native Xorg checks pass under PCSX2 CPU GS emulation. Host
and R5900 models each pass 1,927 shared-backing comparisons across 120
ordered format pairs, plus nine rejection guards. The full host regression
passes 303 cases across 15 suites; seven targeted R5900 cases pass. Native
15-bit converted aliases, remaining formats/operators, full native matrices
and physical memory/performance qualification remain open. See
[the r14 evidence](../build/graphics-hardware-2d-validation-20261015-r14/revision-record.json).

The r15 extension adds native depth-15 RGB555/RGB444 writable format
aliases. GS converts native CT16 storage to packed raw words, synchronizes
both decoded views in scanline store order, and transfers the final raw
result back to CT16. All 24 focused native Xorg checks pass under PCSX2
CPU GS emulation. Host and R5900 models each check every 15-bit raw value
in four layouts, 196 alias comparisons and 25 rejection guards. The full
host regression passes 308 cases across 15 suites; ten targeted R5900
cases pass. Broader native matrices, additional formats, gradients,
extended operators and physical memory/performance remain unqualified. See
[the r15 evidence](../build/graphics-hardware-2d-validation-20261015-r15/revision-record.json).

The r16 extension resolves writable alpha-map aliases through validated
backing storage, including distinct pixmap headers and redirected window
origins. Matching decoded formats share one GS canvas; differing formats
use the GS raw-word coordinator. The full host regression passes 313 cases
across 15 suites, and 11 targeted R5900 cases pass. Both models check 4,038
new requests. The production DDX follows the server header configuration
without forcing Composite. With that module, PCSX2 CPU GS passes 24 native
ordinary-window checks and 24 native15 alias checks. Native redirected
window/backing alias qualification, full native regressions and physical
memory/performance checks remain pending. All-format 2D is unfinished. See
[the r16 backing-alias record](../build/graphics-hardware-2d-validation-20261015-r16/revision-record.json).

The r17 extension adds Screen blending through GS complement lookups and
exact multiplication, including component-alpha masks and writable alpha
maps. It passes all 325 host cases across 15 suites, 16 targeted R5900
cases and 198 native Xorg checks under PCSX2 CPU GS emulation. The 12 new
model cases cover 1,304 requests per environment; native checks cover 27
layouts with three filters and differing depth-15 aliases. Guest software
raster fallback remains disabled. Additional formats, gradients, other
extended operators, full native regressions and physical qualification
remain pending. All-format 2D is unfinished. See
[the r17 Screen record](../build/graphics-hardware-2d-validation-20261016-r17/revision-record.json).

The r18 extension adds GS-only Exclusion blending. Both integer products
remain unrounded until their sum is normalized once; alpha uses Over. It
passes 339 host cases across 15 suites, 21 targeted R5900 cases and 198
native Xorg checks under PCSX2 CPU GS emulation. Each model environment
checks 589,824 exhaustive RGB channels, 1,304 integration requests and 58
rejection guards. The rebuilt multiplication diagnostic also passes
196,608 native channel checks. Pixel arithmetic stays on GS and existing
scratch pages are reused. Additional formats, gradients, other operators,
full native regressions and physical qualification remain pending. See
[the r18 Exclusion record](../build/graphics-hardware-2d-validation-20261016-r18/revision-record.json).

The r19 extension adds GS-only Multiply blending. Exact separately rounded
products and clamped sums stay in GS VRAM, including independent component-alpha
and scalar alpha factors. It passes 351 host cases across 15 suites, 20 targeted
R5900 cases and 198 native Xorg checks under PCSX2 CPU GS emulation. The
native multiplication primitive passes 196,608 channel checks. Twelve new
model cases cover three filters, packed layouts, coverage, alpha maps, shared
backing and solid masks; existing scratch pages are reused. Additional formats,
gradients, other operators, full native regressions and physical qualification
remain pending. See
[the r19 Multiply record](../build/graphics-hardware-2d-validation-20261016-r19/revision-record.json).

The r20 extension adds GS-only Darken, Lighten and Difference. Unrounded
products, comparisons, 17-bit carries/borrows and final normalization stay
on GS, with one additional 8 KiB scratch page. It passes 389 sanitized host
cases across 15 suites, 46 targeted R5900 cases and 594 native Xorg checks
under PCSX2 CPU GS emulation. The native arithmetic probe passes 626,688
channel checks. Each model environment checks 2,654,208 RGB channels, including
316,348 overflows and 193,076 separate-rounding counterexamples. All 112
primitive guards pass, including palette/output alias rejection before any
rendering. Thirty-three operator IDs, gradients, additional formats, full
native regressions and physical qualification remain pending. See
[the r20 ordered-blend record](../build/graphics-hardware-2d-validation-20261016-r20/revision-record.json).

The r21 extension adds GS-only Overlay and HardLight. GS selects each RGB
branch, retains signed unrounded products and performs one final normalization.
It passes 415 sanitized host cases across 15 suites, 39 targeted R5900 cases,
396 native Xorg requests and 417,792 native arithmetic channel checks under
PCSX2 CPU GS emulation. Each model environment checks 2,211,840 RGB channels,
including 114,056 overflow vectors and 419,066 separate-rounding counterexamples.
All 153 new primitive guards pass; no software raster fallback is added.
Thirty-one operator IDs, gradients, additional formats, full native regressions
and physical qualification remain pending. See
[the r21 piecewise-blend record](../build/graphics-hardware-2d-validation-20261016-r21/revision-record.json).

The r22 extension adds GS-only disjoint/conjoint Clear, Src and Dst, plus
DisjointOverReverse. The latter retains wide sampling and RGBA16 solids even
for opaque inputs, preserving its rounding difference from Saturate. It passes
508 sanitized host cases across 15 suites, 104 targeted R5900 cases and 1,412
native Xorg requests across four PCSX2 CPU GS sessions. Both model environments
also pass 489 rejection guards and the explicit opaque-source rounding check.
No software raster fallback is added. Twenty-four operator IDs, gradients,
additional formats, full native regressions and physical qualification remain
pending. See
[the r22 disjoint/conjoint record](../build/graphics-hardware-2d-validation-20261016-r22/revision-record.json).

The r23 extension adds the remaining 17 disjoint/conjoint operators, so all
24 operators in those families now have GS paths. The new ratio factors,
zero-alpha branches, independent binary32 products and final quantization
execute on GS. It passes 713 distinct sanitized host cases across 15 suites,
214 targeted R5900 cases and 104 native Xorg requests in three PCSX2 CPU GS
sessions. The native profiles include signed convolution, component alpha,
original RGBA16 solids, coverage, packed writable maps and native depth-15
differing map views. There is no software raster fallback. Seven blend
operators, gradients, additional formats, broader native regressions and
physical PS2 qualification remain pending. See
[the r23 ratio-factor record](../build/graphics-hardware-2d-validation-20261016-r23/revision-record.json).

The r24 extension adds ColorDodge and ColorBurn through GS binary32
comparisons and conditional selection, preserving zero-alpha branches and
separate product/division rounding. It passes 747 sanitized host cases across
15 suites, 45 targeted R5900 cases and 30 native Xorg checks under PCSX2 CPU
GS emulation. The new primitives also pass 49,152 comparison lanes and
122,880 selected byte lanes per model environment; 4,096 direct blend channel
checks match an independent float oracle. No software fallback is added.
Five blend operators, gradients, additional formats, broader native tests
and physical PS2 qualification remain pending. See
[the r24 ColorDodge/ColorBurn record](../build/graphics-hardware-2d-validation-20261016-r24/revision-record.json).

The r25 extension adds SoftLight with an exactly rounded GS square root
and signed binary32 arithmetic. All 50 previous wide-arithmetic functions
remain unchanged. It passes 767 sanitized host cases across 15 suites,
32 targeted R5900 cases and 20 native Xorg requests under PCSX2 CPU GS
emulation. Per model environment, the new arithmetic passes 43,125 square-root
lanes, 73,728 signed-operation lanes and 2,048 direct blend channel checks.
No software raster fallback or CPU pixel arithmetic is added. Four HSL
operators, gradients, additional formats, broader native tests and physical
PS2 qualification remain pending. See
[the r25 SoftLight record](../build/graphics-hardware-2d-validation-20261016-r25/revision-record.json).

The r26 extension adds HSL Hue, Saturation, Color and Luminosity on GS,
including channel ordering, sequential clipping and exact tiny intermediates.
All 53 defined XRender operator IDs are now admitted for supported pictures;
this does not establish all-format 2D completion. Component-alpha HSL preserves
Dst, matching pinned pixman. The candidate passes 835 sanitized host cases
across 15 suites, 80 targeted R5900 cases and 66 native Xorg requests in
PCSX2 CPU GS emulation. HSL scratch lifetimes were reduced after native
alpha-map tests exposed surface-slot exhaustion. No software raster fallback,
CPU pixel arithmetic, kernel ABI change or .inc file is added. Gradients,
additional formats, broader native tests and physical PS2 qualification
remain pending. See
[the r26 HSL record](../build/graphics-hardware-2d-validation-20261016-r26/revision-record.json).

The r27 extension adds linear, radial and conical gradient sources and masks.
GS evaluates stop interpolation, premultiplication and byte conversion; CPU
work plans coordinates, radial roots, conical angles and uniform coefficients.
All repeat modes and affine/projective sampling have independent byte/float
comparisons. Procedural requests use initialized driver offsets after native
tests exposed uninitialized source offsets in the pinned EXA path.
The candidate passes 853 sanitized host cases across 15 suites, 34 targeted
R5900 cases and 72 native Xorg requests under PCSX2 CPU GS emulation.
No guest software raster fallback or .inc file is added. Additional formats,
broader native tests, memory/performance and physical PS2 qualification remain
pending; Mesa OpenGL/GLX remains unfinished. See
[the r27 gradient record](../build/graphics-hardware-2d-validation-20261017-r27/revision-record.json).

The r28 extension adds ARGB/ABGR2101010 and XRGB/XBGR2101010 pictures at
depth 32. GS performs raw decoding, basic Porter-Duff arithmetic, bilinear
sampling with 16 fractional bits and direct ten-bit quantization/packing.
Sources, masks, component alpha, mixed 32-bit colour, source snapshots,
RGBA16 solids and all three gradient kinds pass 676 model EXA requests.
The candidate passes 862 sanitized host cases across 15 suites, 25 targeted
R5900 cases and 72 native Xorg
requests under PCSX2 CPU GS emulation. No guest software fallback or .inc is
added. Ten-bit advanced operators/Saturate, image convolution, alpha maps,
coverage/16-bit combinations and native depth 30 remain unsupported. Full
native regression, physical PS2, performance and Mesa OpenGL/GLX remain pending.
See [the r28 ten-bit record](../build/graphics-hardware-2d-validation-20261017-r28/revision-record.json).

The r29 extension adds every defined XRender operator to the four ten-bit
formats at depth 32. GS float arithmetic now covers Saturate, disjoint/conjoint
families and all PDF/HSL blends, with nearest/bilinear sampling, colour masks,
component alpha, RGBA16 solids, gradients and source snapshots. Results stay
in GS memory until direct ten-bit quantization. An opaque-target shortcut
was removed for packed requests because zero source alpha with nonzero raw
colour must preserve Saturate semantics, including overlapping aliases.
All 2,756 model requests pass against independent pixman results. The candidate
passes 869 sanitized host cases across 15 suites, 32 targeted R5900 cases and
68 native Xorg requests in PCSX2 CPU GS emulation. All 83 prior wide arithmetic
functions remain unchanged. No guest software fallback or .inc is added.
Ten-bit image convolution, alpha maps, coverage/16-bit combinations, native
depth 30, full native/physical/performance qualification and Mesa GLX remain
unfinished. See [the r29 ten-bit operator record](../build/graphics-hardware-2d-validation-20261017-r29/revision-record.json).

The r30 extension adds original-precision ten-bit image convolution for
all 53 Render operator IDs. The GS performs signed taps, per-tap R5900
unsigned conversion and final clamping; RGB10 and A2 avoid eight-bit
intermediate colour quantization. Source/mask kernels, coordinate bounds
and work budgets are checked before drawing. Raw GS snapshots preserve
overlapping source and mask aliases. Singular transforms also leave Clear
requests untouched, matching pixman. The candidate passes 876 sanitized
host cases, 31 targeted R5900 cases, 64 native Xorg requests and a fresh
32,768-channel Saturate probe in PCSX2 CPU GS emulation. The new model
matrix checks 2,544 convolution requests across all 53 IDs and 27,648
arithmetic channels. An isolated host pixman oracle makes R5900 casts
explicit; target and native tests use unmodified guest pixman. No guest
software fallback or .inc is added. Ten-bit alpha maps, coverage/16-bit
combinations, depth 30, additional formats/filters, full native and physical
qualification, performance work and Mesa GLX remain unfinished. See [the r30 ten-bit convolution record](../build/graphics-hardware-2d-validation-20261017-r30/revision-record.json).

The r31 extension adds GS source/mask alpha-map replacement before ten-bit
nearest, bilinear and convolution sampling. Original A2 precision, decoded
coverage and colour maps use GS extraction and conversion. A ten-bit map
also selects the float path for existing 32-bit parents, RGBA16 solids and
gradients. Aliased parents, masks and maps share one raw GS snapshot. All
7,420 new model requests across 53 operator IDs pass, including 12 map
formats, ignored map transforms/repeat/nested maps, and all four A2 values.
The candidate passes 882 sanitized host cases, 36 targeted R5900 cases and
32 native Xorg requests under PCSX2 CPU GS emulation. Arithmetic sources
and workspace layout are unchanged; all five auxiliary binaries equal r30.
No guest software fallback or .inc is added. Writable ten-bit destination
maps, coverage/16-bit combinations, depth 30, broader formats/filters, full
native/physical/performance qualification and Mesa GLX remain unfinished. See [the r31 alpha-map read record](../build/graphics-hardware-2d-validation-20261017-r31/revision-record.json).

The r32 extension adds GS writable destination alpha maps to the ten-bit
path. Parent and map formats are quantized independently; GS scanline
staging preserves original destination alpha and parent-before-map stores.
Component-alpha HSL retains iterator stores, while explicit Dst and opaque
basic InReverse suppress both stores. All 11,872 new requests across 53
operator IDs and 16 map formats pass, plus 636 input-alias requests covering
both writable backings. The aggregate host regression passes 889 cases in 16
suites; 43 targeted R5900 cases and 64 focused native Xorg parent/map image
checks pass with PCSX2 CPU GS. No guest software fallback, arithmetic/ABI
change or .inc is added. Coverage/16-bit parents/masks/targets, native
depth30, additional formats/filters, full native/physical/performance
qualification and Mesa GLX remain unfinished. See [the r32 writable-map record](../build/graphics-hardware-2d-validation-20261017-r32/revision-record.json).

The r33 extension connects ten-bit rendering to GS coverage, byte/word
and native depth-15 RGB555/RGB444 codecs. Original component precision and
independent parent/map quantization survive nearest, bilinear and signed
convolution. Shifted different-format map aliases preserve scanline stores.
The compositor releases dead upload scratch and borrows twelve sampling
registers for basic blending to stay within the GS surface limit.
All 20,034 new model requests and 24 preflight checks pass per environment;
the full host run passes 908 cases in 17 suites, with 62 targeted R5900
cases and 126 focused native Xorg image checks. No guest software raster
fallback or .inc is added. Native depth 30, further formats/filters and
broader runtime/physical/performance qualification remain open. See [the r33 mixed-format record](../build/graphics-hardware-2d-validation-20261017-r33/revision-record.json).
See [the r7 evidence](../build/graphics-hardware-2d-validation-20261014-r7/revision-record.json)
and [the retained r6 failure](../build/graphics-unorm-validation-20261014/r6-native-failure/record.json).
See [the arithmetic evidence](graphics-wide-arithmetic-validation.json) and
[the native Saturate probe](../build/graphics-float-validation-20261014/saturate-r1/record.json).

Full all-format XRender 2D acceleration remains unfinished. Additional formats,
gradients, extended operators and more-than-eight-bit image
Saturate are unsupported; those paths stop instead of rendering on CPU. The
main client omits 40 filtered coverage Saturate checks; the separate 24-check
coverage mode covers bilinear inputs. The convolution-saturate mode covers
signed kernels and A1/A4/A8 targets separately.
Coverage imports and sampled sources are bounded to 1024 by 1024 pixels, with
further request and VRAM limits. Console memory/performance qualification is
pending. Existing shipping USB/DVD images predate these changes.

Apply `mesa-ps2-hardware-2d.patch` and `xorg-ps2-hardware-2d.patch` after their
BGRA patches; earlier patch files and test packages remain historical evidence.
The r7 kernel adds `ps2-gs-surface-capacity.patch` after the verified
`ps2-gs-rgba-transfer.patch` series.

## Actual evidence

The 2026-10-13 alpha extension supports exact A8 coverage blending,
translucent solid sources, basic Porter–Duff operations on opaque destinations,
nearest affine/projective colour, bilinear A1/A8 masks and A8 glyphs. Core text
now supports all ALUs, RGB plane masks and tiled fills. The production module
and matching kernel pass 726 native Xorg pixel comparisons at depth 15 in 32 MiB
PCSX2, with zero client errors and server exit zero. A separate GS lookup probe
passes 90,112 pixel comparisons. Coverage editing remains on CPU; colour
destinations remain exclusively on GS. See [implementation and limits](graphics-alpha.md)
and [alpha revision evidence](graphics-alpha-validation.json).

The subsequent filter extension adds exact bilinear and signed convolution
colour sampling to the shared Mesa GS backend and production Xorg compositor,
for native RGBX/RGB555 pictures. A1/A8 coverage also accepts convolution. Xorg's
fast/good/best aliases follow its nearest/bilinear definitions. Affine and
projective transforms and all four repeat modes remain supported. A zero
projective denominator now samples transparency, including with repeat enabled.

The updated production module passes **834 native Xorg comparisons**, including
500 Render comparisons against pixman, at depth 15 in the 32 MiB PCSX2 guest.
The client and Xorg exit successfully with zero client errors. The lookup probe
still passes 90,112 native pixel comparisons. All 168 component cases pass under
host ASan/UBSan and R5900 Linux-user execution; the client oracle passes 834
checks at both Xvfb depths 15 and 24. See the
[filter validation record](graphics-filters-validation.json) and
`build/graphics-filters-20261013` for the exact module, kernel, clients and logs.

Bilinear sampling uses three GS byte planes for weighted products and carries,
then truncates the combined four-tap sum once using pixman's seven-bit weights.
Convolution uses four planes for signed 16.16 coefficients, wrapping 32-bit
accumulation, half-unit rounding and signed saturation. Palette lookups and
native transfers perform every colour calculation; compositing performs no
colour readback. CPU work computes coordinates, constant lookup tables and
source coverage. This qualification does not enable Gallium's general linear
blit or texture sampler, or Mesa shader/OpenGL/GLX execution.

Filtering reserves at most 128 KiB of GS scratch storage including alpha work
and the palette. A filtered request stores one coordinate row, up to 1 MiB of
coverage and up to 1 MiB of copied convolution parameters per source/mask;
the resulting CPU plan is at most 3,170,304 bytes. Nearest colour with a
convolution mask can use a plan of at most 6 MiB. Convolution work is bounded by
16,777,216 taps per source/mask request. These limits do not measure total or
peak desktop RAM or interactive performance. Apply the Mesa/Xorg
`*-ps2-filters.patch` deltas after their alpha series; no additional kernel
change is required beyond `ps2-gs-lut.patch`. Exported sources match the native
build and compile with Xorg COMPOSITE enabled and disabled.

The format extension supports both red/blue orders of opaque RGBX and RGB555:
`x8b8g8r8`, `x8r8g8b8`, `x1b5g5r5`, and `x1r5g5b5`. All sixteen source/destination
combinations support nearest, bilinear and signed convolution, transforms and
repeat modes, A1/A8 masks, translucent solids and alias snapshots. Channel
reordering runs entirely on GS using a typed RGBA palette mapping operation.
Its full-byte alpha mapping also provides a primitive for further ARGB work;
this does not yet enable ARGB pictures in Xorg.

The matching production module and kernel pass **998 native Xorg comparisons**,
including 664 Render comparisons against pixman, at depth 15 in 32 MiB PCSX2.
The RGB lookup diagnostic passes 90,112 comparisons and the new RGBA mapping
diagnostic passes 294,912, covering all four source bytes, full alpha values,
partial masks and rectangle edges. All 177 component cases pass under host
ASan/UBSan and R5900 Linux-user execution. The 998-check client oracle passes
at Xvfb depths 15 and 24 with both pixmap depths enabled. See
[format validation](graphics-formats-validation.json) and
`build/graphics-formats-20261013` for the exact artifacts and logs.

Apply `ps2-gs-map.patch` after `ps2-gs-lut.patch`, and the Mesa/Xorg
`*-ps2-formats.patch` deltas after the filter series. The exported source files
match the production build and compile with COMPOSITE enabled and disabled.
Reordering reuses existing GS scratch surfaces, retaining the 128 KiB filter
scratch limit and the previous CPU plan limits. No colour readback or CPU
colour renderer is introduced.

The RGBA extension adds owned depth-32 core pixmaps with all sixteen Boolean
ALUs and full/partial alpha plane masks, including copies, overlap, tiles,
solid fills, CopyPlane and readback. Render accepts `a8b8g8r8` and `a8r8g8b8`
with varying alpha, thirteen basic Porter-Duff operators, nearest/bilinear/
convolution, transforms, repeats, A1/A8 masks, solids and aliases. GS isolates
alpha before coverage readback; original RGB pixels never reach a CPU renderer.
Colour arithmetic and destination writes remain on GS.

The exact kernel/module pass **1,442 native Xorg pixel checks**, including 948
Render comparisons against pixman, with zero client errors and Xorg exit 0 in
the 32 MiB guest. The GS probe additionally passes 1,105,920 rectangle-map,
491,520 raw32-solid and 5,063 isolated-alpha comparisons, along with the earlier
lookup/mapping probes. All 187 component cases pass under host ASan/UBSan and
R5900 Linux-user execution. See [RGBA validation](graphics-rgba-validation.json)
and `build/graphics-rgba-20261013` for the exact binaries and logs. Apply
`ps2-gs-map-blit.patch` after the map series and Mesa/Xorg `*-ps2-rgba.patch`
after the format series. Exported sources compile with COMPOSITE on and off.

This run uses PCSX2's **GS software host renderer**, executing the native guest
driver and GS packets. The OpenGL host renderer fails a raw32 alpha comparison
with the same kernel and GS probe; its cause remains unresolved, and the failed
comparison is retained in the validation record. These results do not establish
physical GS behavior. Raw32 Boolean work reserves at most 80 KiB of GS scratch;
filtered ARGB work uses at most 144 KiB including alpha isolation. The largest
nearest plan is 8 MiB plus a 1 MiB source-alpha snapshot; filtered plans are at
most 5,267,456 bytes plus that snapshot. Post-exit meminfo does not measure peak
desktop RAM or interactive performance.

The coverage extension connects owned A1/A8 sources to all six RGB/ARGB colour
layouts through GS, and RGB/ARGB source alpha to owned A1/A8 destinations.
Both directions support transforms, repeats, masks and all three filters.
Coverage destinations also accept Saturate through pixman's existing coverage
path. Every colour calculation remains on GS; only isolated alpha enters CPU
coverage processing. The exact production module passes **2,414 native Xorg
checks**, including 1,920 Render comparisons, with zero errors and Xorg exit 0
using the GS software host renderer. All 193 component cases pass in each
host/R5900 environment. See [coverage validation](graphics-coverage-validation.json)
and `build/graphics-coverage-20261013`; apply Mesa/Xorg `*-ps2-coverage.patch`
after the RGBA series. The kernel is unchanged. A prepared colour-to-coverage
snapshot and a coverage-to-colour request each use at most 1 MiB of CPU storage;
the black colour source uses one additional 8 KiB GS page. The OpenGL alpha
failure and the physical/performance limits recorded above still apply.

The requested completion scope includes all XRender formats and filters. Other
colour formats/layouts, component alpha, gradients, alpha maps, ARGB Saturate
and extended operations remain unfinished. Filter support currently applies to
the six RGB/ARGB layouts and A1/A8 coverage. Physical, peak RAM, performance and
Mesa shader/OpenGL/GLX qualification remain open. Shipping USB/DVD images have
not been rebuilt with this extension.

The following plane/composite evidence describes the preceding revision.

The 2026-10-13 extension adds colour-source `CopyPlane` to native RGBX/RGB555
and owned monochrome destinations. Every native source plane, all sixteen
ALUs, RGB plane masks, overlapping copies and MI clipping are covered. GS
isolates the source plane; the driver serializes those source bits and draws
colour destination runs with GS Boolean operations. CPU access to colour
destinations is never enabled. Depth-1 destination editing remains confined
to the existing owned monochrome storage.

The shared Mesa/Xorg compositor now supports opaque `PictOpSrc` and
`PictOpOver`, nearest affine transforms, RepeatNone/Normal/Pad/Reflect and
owned A1 masks. CPU work prepares sample geometry and examines existing
source bitmap bits; textured GS triangles produce colour pixels. Front or
aliased sources are snapshotted in GS VRAM. Compositing performs no colour
readback. The texel plan is bounded by 1048576 pixels (4 MiB of main RAM),
with sources at most 1024x1024. This is a request limit, not a measured total
desktop memory budget. Extended sampling requires source pixmaps with zero
drawable origins; ordinary untransformed copies retain their previous route.

Seventeen CopyPlane cases and 24 composite cases pass against independent
pixel models and pixman; related regressions, warning-clean native module
compilation and exported patch validation are recorded in
[current evidence](graphics-plane-composite-validation.json). The expanded
X11 client adds colour/bitmap CopyPlane and 48 Render comparisons to the
previous 141 checks, for 333 checks per depth. Its oracle passes against
Xvfb at depths 15 and 24; that result alone does not qualify GS execution.
The corrected native Xorg run also passes all 333 comparisons at depth 15
in 32 MiB PCSX2, with zero client errors and server exit zero. The run found
and fixed EXA rejecting an owned A1 mask before PrepareComposite: owned
monochrome storage now advertises that native callbacks can consume it,
while colour CPU access remains rejected. The production module excludes
diagnostic clients. All 123 relevant cases pass in each host sanitizer and
R5900 Linux-user environment. This run does not add physical, RGBX native
runtime or new interactive TWM/xterm qualification. The saved meminfo is
captured after Xorg exits, not a peak desktop RAM measurement.

At that preceding revision, eight-bit alpha, component alpha, projective
transforms, bilinear filtering and additional Render formats remained open.
The current remaining scope is recorded above and in the alpha validation record.

The 2026-10-09 GS-only extension adds native colour-source Boolean copies, all sixteen
tile ALUs with RGB plane masks, tiled polygons, and read-only colour
`GetImage`/`GetSpans`. Drawing and plane masking execute on GS; client readback only
transfers native pixels and serializes protocol replies. The Boolean engine reserves at
most 56 KiB of GS VRAM, splits work into 64x32 rectangles, and preserves overlap order
and the live Mesa bindings. The kernel adds bounded masked texture transfers and 8 KiB
VIF1 FIFO downloads. The production DDX also installs the opaque XRender `PictOpSrc`
copy adapter. At that revision, colour-source `CopyPlane`, XRender alpha,
masks and transforms, other formats, and physical qualification remained open. Mesa shader
and GLX execution remain unsupported.

The subsequent display fixes disable PCSX2 clear shortcuts while retaining its
OpenGL hardware renderer, and restore the native cursor background with GS
textured triangles. The user confirms clean window backgrounds/title bars and
the disappearance of the black cursor-sized rectangle in revision `r9`.
The demo also repaints after its managed resize, prints its complete label,
and waits for xterm to become viewable before assigning keyboard focus.
The native pixel test now covers 141 comparisons, including solid/tiled GC
state changes, visible-window fills and core text against a monochrome font
reference. These tests use CPU pixel arithmetic only as a read-only oracle;
production drawing remains entirely on GS. See
[runtime and display evidence](graphics-gpu2d-runtime-validation.json).

The 2026-10-05 native DDX now runs an actual Xorg/TWM session in the project
PCSX2 runtime with 32 MiB guest RAM. Four drawing scenes, monochrome
CopyPlane, cursor movement at screen edges, attached mouse slave motion,
managed-window resizing, xterm scrolling and interactive typing/backspace
completed with zero reported errors; xterm remained alive and Xorg exited
with code 0. All 273 component checks passed on host ASan/UBSan and R5900. The
cursor regression resolves callback ownership through the master pointer,
so the attached physical mouse does not appear to be a foreign device.
An additional cursor fix accepts Xorg's valid empty-mask glyph cursor. xterm
uses this cursor to hide the pointer while typing; rejecting its zero-size
bitmap previously caused `X_CreateGlyphCursor` to return `BadAlloc` and close
the terminal. Empty cursors now hide the native cursor while retaining their
normal lifecycle. Visible cursor geometry checks remain in place. The user
provided a successful extended-run log after this fix. See
[interactive terminal evidence](graphics-core2d-terminal-validation.json) and
[cursor-fix session evidence](graphics-core2d-cursor-fix-validation.json).
The earlier extended run's incomplete visual observation remains recorded
in [its original evidence](graphics-core2d-xorg-validation.json).

The first hardware candidate booted to Xorg but rejected the attached mouse as a foreign
cursor device. Its replacement, `hardware-cursor-fix`, passed the complete emulator test
but froze on physical hardware before Xorg, while waiting for IOP ROM-module loading.
That failure is separate from the fixed cursor callback. The revised kernel removes
cached EE access to live IOP RAM and publishes the LOADFILE patch with ordered word
stores. It includes the empty-cursor fix and a kernel rebuilt from clean sources with
`W=1`, `CONFIG_WERROR=y` and `KCFLAGS=-Werror`: 850 C objects, 22 assembly objects and
11 modules, with zero compiler, kernel-doc, modpost or linker warnings. The complete
emulator session passes with 32 MiB guest RAM. Physical retesting of these exact
artifacts is pending. Diagnostic logs persist under `/var/lib/ps2-xorg-test`;
`keyboard-input.ok` contains `ps2-input-ok` when the typed command succeeds. That marker
is not an error. That revision left colour-source CopyPlane and fuller XRender
alpha/masks/transforms for subsequent implementation and qualification. The 2026-10-09
colour readback, Boolean copy and tile extensions are described above and require
separate qualification of their exact artifacts. The kernel now enables inotify and
eudev initializes in the emulator; physical mouse input, input hotplug and a complete
physical Xorg session remain to be qualified.

| Component | Evidence | Remaining qualification |
| --- | --- | --- |
| GNU console and SMAP | User confirms login, DHCP, ping and DNS on SCPH-90001 | Other models, sustained traffic and recovery |
| Xorg/TWM packages | Cross-build/target ELF audit and actual native core-X integration session pass in PCSX2 | Full visual confirmation and physical X session |
| PS2 theme | Actual host Xvfb/TWM preview | PS2 rendering and RAM measurements |
| GS commands | Host ASan/UBSan, PCSX2 probe and corrected physical scene | Further formats/modes and application rendering |
| GS visual probe | Corrected 720x576 CT32/Z16S scene shows continuous triangles on real PS2; reconfirmed in the 2026-10-04 hardware report | Additional formats/modes and application rendering |
| GS image upload | User confirms displayed image and return to text on SCPH-90001 | Pixel readback and other formats |
| Owned GS surfaces | Kernel and native Xorg/Mesa storage components cross-build; independent-surface scene confirmed in PCSX2 and on real PS2 | Further formats, full DDX/context, provider recovery and kernel lockdep |
| Owned GS rendering | Allocated depth, Mesa resource/raster adapter and triangle stage cross-build; ten host/R5900 cases pass; owned color/depth diagnostic confirmed in PCSX2 and on real PS2 | Other formats and complete OpenGL context |
| Native RGB sampling | Kernel STQ/cache/clamp packets, Mesa sampler views/textured stage and EXA texture copies cross-build; eleven host/R5900 checks pass; corrected perspective panels confirmed in PCSX2 and on real PS2 | Pixel readback, further states/formats and full desktop |
| Xorg drawing components | Native EXA storage/fill/copy/upload callbacks, core bitmap glyph callbacks with composite clipping, and shared Mesa queue ordering cross-build | Complete DDX, further raster operations, alpha glyphs, cursor and VT ownership |
| Mesa drawing components | Color/depth resource references, native offscreen triangles and bounded RGB sampling work in the diagnostic | Shader/state execution, GLX and OpenGL application tests |
| Mesa context and Xorg lifetime | Actual screen/context callbacks, deferred resource destruction and Xorg leases pass nine host/R5900 cases; context diagnostic completes in PCSX2 and is user-confirmed on real PS2 | Shader `draw_vbo`, full DDX, presentation and stress/recovery |
| Gallium resource copy callback | Actual `resource_copy_region` uses the native GS adapter; seven new cases and 39 shared Xorg/context regressions pass on both host and R5900 | New callback runtime pixel qualification and application integration |
| Native core glyphs | Actual Xorg `PolyGlyphBlt`/`ImageGlyphBlt` callbacks and GS bitmap plans pass eleven host/R5900 cases; three-row diagnostic confirmed in PCSX2 and on real PS2 | Further formats, full DDX installation and Render alpha glyphs |
| GC lifecycle and core text | Screen/GC lifecycle hooks, ordinary 8/16-bit text routing and mutable lower callback tables pass eleven new host/R5900 cases; core-text diagnostic confirmed in PCSX2 and on real PS2 | Full DDX installation, additional raster states and stress/recovery |
| Solid Boolean drawing | All sixteen constant-source ALUs, native RGB plane masks and EXA/Mesa ordering pass thirteen host/R5900 cases; grid/XOR diagnostic confirmed in PCSX2 and on real PS2 | RGB555/partial masks, source-pixmap ALUs and full desktop integration |
| Native window painting | Solid/tiled background and border `PaintWindow`, inherited alignment, child clipping and redirected RGBX/RGB555; 51 component and five probe checks pass per environment; four PCSX2 scenes and console return user-confirmed | Physical qualification and actual X server/Damage lifecycle |
| Native tiled GC fills | Actual `FillSpans`/`PolyFillRect`, pattern phase, composite clipping, redirected RGBX/RGB555 and pixel-tile Boolean drawing; 74 component and five probe checks pass per environment; four supplied PCSX2 panels match and user confirms console return | Physical qualification, further tile ALUs/masks, stippled fills and full X server/Damage execution |
| Native opaque stippled GC fills | Foreground/background pattern construction on GS from monochrome source bits; clipped/redirected RGBX/RGB555 rectangle/span output; 88 component and five probe checks pass per environment; four supplied PCSX2 panels match and user confirms console return | Physical pixels, full EXA/fb bitmap client execution and actual X server/Damage execution |
| Owned monochrome sources | Zeroed, bounded depth-1 EXA storage, hidden-pointer source views and lower bitmap GC routing; 98 component/five probe checks pass per environment; four PCSX2 panels and console return user-confirmed | Actual complete EXA/fb client execution, physical/RGB555 runtime and full DDX/GLX |
| Native transparent stippled GC fills | Zero bits preserve destination; all 16 Boolean ALUs/partial RGB masks, clipping, overlap order and redirected RGBX/RGB555 pass 114 component checks per environment; distinct four-scene probe passes six lifecycle checks per environment and completes PCSX2 with exit zero/text-mode restoration; all four panels match and user confirms console return | Physical/RGB555 runtime, fine-pattern performance and actual X server/Damage integration |
| Opaque stipple Boolean ALUs/masks | Disjoint zero/one runs apply each pixel operation once; all 16 ALUs and RGB masks, overlap order and redirected RGBX/RGB555 pass 130 component checks per environment; optimized copy repeat remains; distinct probe passes six lifecycle checks per environment and completes PCSX2 with exit zero/text-mode restoration; all four supplied panels match | User console confirmation, physical/RGB555 runtime, fine-pattern performance and actual X server/Damage integration |
| Native thin solid lines | All eight octants, 256 screen biases, original clipped phase, caps/XOR joins and redirected RGBX/RGB555; 12 new plus 130 component regressions and six probe lifecycle checks pass per environment; all four supplied PCSX2 panels match and user confirms console return | Physical/RGB555 pixels, wide lines/arcs, performance and actual X server/Damage |
| Native thin dashed lines | On/off and double dash phase across clipping/joins/corners, native coloured Boolean runs and redirected RGBX/RGB555; 16 new plus 142 regressions and six probe lifecycle checks pass per environment; four PCSX2 command/fence scenes complete with exit zero/text-mode restoration | Physical/RGB555 pixels, patterned dash fills, wide lines/arcs, performance and actual X server/Damage |
| Native CopyArea | Actual Xorg callback with upstream `miDoCopy`, native Mesa band copies, overlap ordering and odd-width tails pass twelve new host and thirty R5900 checks; full copy sequence user-confirmed in PCSX2 | Real hardware, further copy ALUs/masks and full DDX |
| EXA copy integration | Actual `PrepareCopy`/`Copy`/`DoneCopy` use the shared Mesa GS adapter; eight new adapter cases and sixteen copy regressions pass on host and R5900; pinned Xorg patch compiles | New runtime pixels, upstream window lifecycle and full DDX |
| Gallium blit and EXA composite | Native nearest scaling, source flips and scissoring; opaque EXA source copies share the adapter; 41 component and four diagnostic lifecycle cases pass per environment; PCSX2 scene panels match supplied screenshots | Physical/RGB555 runtime pixels, further composite states and full DDX/GLX |
| Native screen window copy | Shared Mesa GS copy batch; 52 component cases and four lifecycle cases pass per environment; all four PCSX2 scene command checks exit zero and restore console; user confirms matching panels | Physical/RGB555 runtime pixels, actual Damage/X server lifecycle and TWM moves |
| Native solid GC requests | Six real GC callbacks and Mesa native batch pass thirteen new host/R5900 cases and eleven GC/text regressions; panel/XOR diagnostic confirmed in PCSX2 and on real PS2 | RGB555/partial-mask runtime pixels, wide/dashed lines and full TWM session |

### Real PS2 diagnostic acceptance, 2026-10-04

The user explicitly confirms that **the ten graphics runtime
diagnostics work on real PlayStation 2 hardware**. The earlier reported
console is SCPH-90001, with a PS2-to-HDMI monitor setup and stable 576p boot.
The report covers the normal scenes and return to the text console:

| Diagnostic | Real hardware verification |
| --- | --- |
| `ps2-gs-test` | User-confirmed pass: Rectangles and continuous depth-overlapped triangle |
| `ps2-gs-upload-test` | User-confirmed pass: Native image upload |
| `ps2-gs-surface-test` | User-confirmed pass: Independent owned surfaces and transfers |
| `ps2-gs-render-test` | User-confirmed pass: Owned color/depth triangle rendering and occlusion |
| `ps2-gs-texture-test` | User-confirmed pass: Updated texture sampling and corrected perspective checker grid |
| `ps2-gs-context-test` | User-confirmed pass: Mesa screen/context and shared Xorg resource lifetime diagnostic |
| `ps2-gs-glyph-test` | User-confirmed pass: Transparent/background glyph rows and deliberate composite clip gap |
| `ps2-gs-gc-test` | User-confirmed pass: Core text through GC lifecycle and clipping callbacks |
| `ps2-gs-logic-test` | User-confirmed pass: Sixteen Boolean ALU tiles and reversible XOR outline |
| `ps2-gs-solid-test` | User-confirmed pass: Six solid GC callbacks, panel and three reversible XOR outlines |

[Hardware verification](graphics-hardware-validation.json) records the dated
user confirmation. Device logs, individual exit codes, binary hashes read back
on the console, pixel readback and full desktop memory measurements were not
captured. The result qualifies these ten diagnostics on the user's PS2; full
Xorg/TWM and existing OpenGL applications remain unfinished.

Each component JSON has a `physical_hardware_followup` referencing this report.
Its earlier build/emulator results and physical flags retain their original
scope. Recorder scripts retain the dated follow-up during regeneration.

## Kernel patches and experimental ABI

The current [r13 batch qualification](graphics-gpu2d-batch-validation.json)
passes 141 native pixel checks and TWM/xterm interaction in 32 MiB. It groups
solid, render and texture operations into bounded GS DMA packets to reduce
repeated completion waits. The production applications, XDG menu, clock and
test-free media are described in [the PAL desktop guide](pal-desktop-media.md).

Apply the current console/platform patch sequence, then:

1. `kernel/patches/ps2-gs-framebuffer.patch`
2. `kernel/patches/ps2-gs-commands.patch`
3. `kernel/patches/ps2-gs-surfaces.patch` (owned offscreen storage extension)
4. `kernel/patches/ps2-gs-rendering.patch` (owned triangles and allocated depth)
5. `kernel/patches/ps2-gs-textures.patch` (bounded native RGB texture sampling)
6. `kernel/patches/ps2-gs-logic.patch` (constant-source Boolean drawing)
7. `kernel/patches/ps2-gs-transfer.patch` (masked texture replacement/add/subtract)
8. `kernel/patches/ps2-gs-readback.patch` (bounded native read-only VIF1 transfers)
9. `kernel/patches/ps2-gs-batch.patch` (ordered bounded DMA batches and solid batch ABI)

The first adds demand-paged CPU memory and bounded GIF image uploads. GS VRAM
is not mapped into the CPU address space. Upload mapping is not hardware
rasterization; mapped buffers exclude native commands, including unfaulted and
forked VMAs.

The second installs `linux/ps2-gs.h`:

- `PS2GS_GET_CAPS` reports the mode, available depth space and completed
  hardware counters.
- `PS2GS_SUBMIT` accepts at most 64 fixed-size commands: fills, aligned local
  copies, Gouraud triangles and depth clear.
- `PS2GS_UPLOAD` transfers up to 3072 bytes of packed native CT16/CT32 pixels
  to a bounded rectangle in the current framebuffer. The kernel snapshots the
  source, pads the last GIF quadword with zero and waits for FINISH. A new
  capability bit enables it without changing existing ABI-1 structure sizes.
- Triangle coordinates are unsigned screen-space 12.4 fixed point. Depth is
  16-bit, with greater values nearer. Transformation, textures, blending,
  arbitrary depth functions and shaders are not implemented.
- Context 2 drawing explicitly establishes PRIM attributes, framebuffer,
  offset, scissor and depth state.
- The entire batch is validated before drawing. Unsupported states return
  errors; mode changes reject stale submissions.
- Submission requires `CAP_SYS_RAWIO`. Users cannot submit raw registers,
  DMA addresses or VRAM pointers. Console locking protects font/pan changes.
- Each command waits for a GS FINISH fence. DMA completion is insufficient.
  Hardware timeouts stop the batch; earlier commands may have drawn.

The framebuffer endpoint targets the existing display buffer. The third patch
adds a separate file-owned surface endpoint, and the fourth connects triangles
and allocated depth to that same owner. The fifth adds bounded texture
sampling, described below. Further sampler states and readback still need work.
It must not be advertised as a complete Xorg or Mesa driver.

## PS2tek review

[PS2tek](https://psi-rockin.github.io/ps2tek/) guides checks of PRIM context and
shading bits, PRMODECONT selection, XYZ2 drawing kicks, fixed-point coordinates,
FRAME/ZBUF base units, scissor endpoints, transfer registers and GIF IMAGE
payloads. Tests use
the original imported GS/GIF structures rather than a separate packing model.

Future VIF/VU and texture work must handle ownership, texture-cache invalidation
and GIF path coordination explicitly. The root [TODO](../TODO.md) tracks those
integrations and their acceptance criteria.

## Native Xorg and Mesa drawing components

`graphics/ps2_gs_client.c` queues at most 64 typed commands and fences through
the existing kernel interface. It retries only the driver's pre-submission
`EAGAIN`; errors that can follow drawing are sticky and never replayed.
The transport checks mode identity, bounds, local-copy alignment and the native
RGB555 layout. Its upload path checks source length/stride and pointer overflow,
packs rows without their padding, and splits wide or tall images into bounded
GIF transfers. Drawing queued before an upload is fenced first. It does not map
GS VRAM or replace failed drawing with a CPU copy.

`desktop/xorg/ps2_exa.c` provides actual EXA callback signatures for front-buffer
`GXcopy` fills/copies, full plane masks, synchronous fences and `UploadToScreen`
for native front-buffer pixels. CPU access is
explicitly rejected. Offscreen pixmaps, arbitrary copy widths, other raster
operations and glyphs are not supported yet. These callbacks are not registered
as a usable DDX: unsupported X operations would otherwise terminate the server.

`mesa/ps2/ps2_draw.c` provides Mesa's final `draw_stage` callbacks, accepting
transformed and clipped position/color vertices and submitting GS triangles.
`ps2_raster.c` converts window coordinates and depth and supplies color clears
with Gallium's exclusive scissor bounds. Its front-buffer transfer adapter
accepts native `R8G8B8X8_UNORM` and `R5G5B5X1_UNORM` images without CPU format
conversion; it is not yet `pipe_context.texture_subdata` or texture allocation.
Vertex transformation may run in Mesa's
CPU geometry stage; pixel rasterization uses the GS. The component rejects
unclipped triangles and unsupported points/lines and does not advertise an
OpenGL profile. It is not a complete Gallium screen/context or a GLX driver.

Seventeen host sanitizer cases cover the transport, Xorg/Mesa callbacks and the kernel
upload validator/packet builder. The same seventeen cases execute as R5900 Linux
programs with wrapped GPU syscalls or pure packet functions. The previous PCSX2
mode-selector and GS primitive validator checks also passed. Three additional host/R5900
probe lifecycle checks cover success, a hardware error and Ctrl+C, including
text-console restoration and failure without replay. These tests establish callback
behavior, not a physical Xorg session or OpenGL conformance. See [development
evidence](graphics-development-validation.json).

Generic source-only patches are `userland/patches/xorg-ps2-raster.patch` and
`userland/patches/mesa-ps2-raster.patch`. They add the components to the matching
Xorg/Mesa source trees without enabling an incomplete driver.

## GS image-upload candidate

It includes the new upload kernel and `ps2-gs-upload-test`; the qualified console image
and kernel source are preserved. The default remains 576p/50, with GNU tools, ext4, UK
keys, native SMAP and login `root` / `eelinux`. Xorg/Mesa are not enabled.

Use a spare USB stick for this diagnostic image. It briefly runs the primitive
and upload probes before text startup. After login, run:

```sh
ps2-gs-upload-test
```

For four seconds, expect red, green, blue and white bars above a black/white
checkerboard on navy. Then the text console returns. The odd 131x65 image and
padded source rows exercise GIF padding and several bounded transfers.
`PASSED` means transfer and FINISH completion, not verified output pixels;
The user confirmed on the SCPH-90001 that the image displays correctly and
returns to the terminal. Pixel readback and a format sweep remain untested.
Ctrl+C restores text mode.

The startup service saves its output to `/run/ps2-gs-upload.log` and forwards it
to kernel logging and the text console. `/dev/kmsg` reaches the serial kernel
console without repeatedly opening the polled UART tty: its inherited
`tx_empty` callback always returns zero, so short-lived tty writers can delay
at close or lose their diagnostic output. This image changes logging only;
the UART driver's lifecycle remains separate review work.
The candidate build, patch application,
image checks and controlled emulator result are recorded in
[upload evidence](graphics-upload-validation.json).

## Owned GS surfaces for Xorg and Mesa

`ps2-gs-surfaces.patch` adds `/dev/ps2-gs`, mode 0600. One privileged open file
description owns the GS; `dup` shares that session and the last close releases
its allocations. While it is open, legacy framebuffer drawing and CPU mappings
are excluded. The text console retains its reserved storage and context 1.

The kernel allocates at most 64 opaque handles from whole 8 KiB GS pages,
outside the padded display buffer, swizzled console font/palette and reserved
depth buffer. It clears all allocated pixels, including padding, before
publishing a handle. CT16 and CT32 surfaces can have different strides.
Typed fills, native image uploads and same-format local copies accept handles,
never user-selected GS addresses. Handle zero identifies the front buffer.
Copy widths still require the GS format alignment.

The active VT must be in `KD_GRAPHICS` for operations, and front-buffer pan
offsets must be reset before opening the session. Mode, font or pan changes
make the session stale; close and reopen it. Hardware failure poisons the
session instead of replaying drawing. The device's separate reference and
disconnect lock keep surviving files from touching a removed framebuffer.
Provider lifecycle checks exercise that code with a syscall/locking scaffold;
they do not replace kernel lockdep or physical removal testing.

`graphics/ps2_gs_surface.c` supplies the shared transport and validates kernel
allocation replies, bounds, formats, stride and source length. Xorg's
`ps2_surface_exa.c` now provides actual EXA `CreatePixmap2`, `DestroyPixmap`,
header, fill/copy and upload callbacks for offscreen storage. Mesa's
`ps2_resource.c` creates real `pipe_resource` objects for level-zero native
2D color images, with upload, copy and cleanup. No CPU pixel mapping or software
raster fallback is added. These remain components: glyph/composite operations,
DDX screen/cursor/VT setup, complete Mesa screen/context, texture sampling and
GLX integration are unfinished. This third patch exposes color storage and
transfers. The separate fourth patch adds owned rendering, described below.

The source-only patches `xorg-ps2-surfaces.patch` and `mesa-ps2-surfaces.patch` apply
after their respective raster patches. Fourteen new cases pass on the host with
ASan/UBSan and as R5900 Linux binaries: allocation/fragmentation, packet encodings,
ownership/disconnect, native Xorg/Mesa callbacks, and the actual probe's
success/error/SIGINT console recovery.

It retains the 576p GNU console, ext4, SMAP, UK keys and `root` / `eelinux`; it does not
enable Xorg. After booting it on a spare stick, run:

```sh
ps2-gs-surface-test
```

Expect a cyan rectangle on the left and red/green/blue/white bars above a
black/white checkerboard inside a black border on the right, over navy.
The border is intentional: B is cleared to black, and the smaller uploaded
image is copied into only part of it. It is not missing display output.
The console returns after four seconds. The probe uploads into A, copies A
into the differently sized B, then fills A with cyan before displaying both.
B must retain the original image. This tests independent storage; a FINISH
result alone does not verify those pixels. Startup output is saved to
`/run/ps2-gs-surfaces.log`.

Image assembly refuses to overwrite an existing image. Preserve its test
record before choosing a new output for another candidate.
See [surface evidence](graphics-surface-validation.json).

## Owned Mesa rendering and Xorg ordering

`ps2-gs-rendering.patch` extends the same exclusive `/dev/ps2-gs` file with
allocated Z16/Z16S depth and bounded fill/triangle/depth-clear batches. Color
and depth have distinct roles in the opaque handle namespace. A matching
color/depth pair must have identical dimensions and padded stride. The kernel
clears the entire padded depth allocation before publishing its handle and
validates the whole batch before drawing. A failed submission poisons the
session; drawing is never replayed.

The native transport in `graphics/ps2_gs_render.c` uses the existing surface
client's file and fences queued rendering before switching targets. Mesa's
`ps2_render_target.c` retains real Gallium color/depth resource references,
implements color/full-depth clears and submits window-space triangles.
`ps2_owned_draw.c` supplies the final Mesa triangle-stage callback. This stage
expects geometry already transformed and clipped. Depth uses reversed 16-bit
values and GS GEQUAL; blending, textures and other depth functions remain
unsupported.

Xorg's `ps2_render_bridge.c` wraps the real EXA callbacks and fences the shared
Mesa queue before 2D drawing or storage changes. This establishes ordering for
components sharing a file in one process; it does not implement DRI or a
complete Xorg DDX. The source-only `mesa-ps2-rendering.patch` and
`xorg-ps2-rendering.patch` apply after their respective surface patches.

Ten new cases pass with host ASan/UBSan and as R5900 Linux binaries: depth
roles and padding, whole-batch rejection, actual GS packets, failure/stale
sessions, native transport, Mesa resource references/final draw callbacks,
EXA ordering, and the actual diagnostic's success/error/SIGINT cleanup.
The target build selects Mesa's GCC atomic builtins and links GNU `libatomic`;
the actual resource-reference operations execute in the target tests.
Wrapped device calls and the reduced kernel locking scaffold do not establish
GPU pixels or kernel lockdep behavior.

It retains the 576p GNU console, ext4, native SMAP, UK keys and `root` / `eelinux`. Four
startup probes run. To repeat the new one:

```sh
ps2-gs-render-test
```

This diagnostic uses real Mesa color/depth resources and the raster adapter,
renders to a 320x256 offscreen target, then copies it to the display. Expect
cyan over a larger red triangle, no yellow triangle, then the text console.
The user confirmed that exact scene and restoration in PCSX2. The controlled
emulator log records all four probes passing and SMAP link-up without panic.
The 2026-10-04 hardware follow-up confirms this owned-rendering diagnostic on
the real PS2 in the hardware verification report. This is not an OpenGL
application or a working `startx` desktop. Startup output is saved to
`/run/ps2-gs-render.log`.

Packaging refuses to overwrite an existing image. The earlier qualified
images and kernel sources are preserved. See
[rendering evidence](graphics-render-validation.json).

## Native texture sampling components

`ps2-gs-textures.patch` extends the owned file with bounded textured triangles.
The texture must be a distinct nonzero color handle belonging to the same
owner, with CT16/CT32 native RGB pixels and dimensions up to 1024. Depth roles
cannot be sampled. The kernel snapshots and validates the whole batch before
drawing, including flags, coordinates, target identity and matching depth.
It accepts bounded binary32 STQ bit patterns without executing kernel floating
point instructions. Context 2 establishes the sampler, logical region clamp,
nearest filtering, RGB replace/modulate and depth state. TEXFLUSH before each
textured triangle invalidates data left by uploads, copies or rendering.

`mesa/ps2/ps2_sampler.c` creates actual Gallium sampler views and retains their
color resources. It fences before uploads, converts logical non-power-of-two
texture coordinates to TEX0's rounded dimensions, and derives perspective STQ
from normalized coordinates and reciprocal clip W. The GS performs interpolation,
sampling and pixel drawing. `ps2_textured_draw.c` retains the view and supplies
the final Mesa triangle callback for transformed/clipped geometry. Views accept
native RGB, identity RGB swizzles and constant-one alpha, level zero and no array
layers. This component supports nearest/clamp-to-edge with bounded coordinates;
other filters, wrapping, texture alpha, mipmaps and shaders remain unsupported.

`desktop/xorg/ps2_texture_exa.c` provides real EXA `CheckComposite`,
`PrepareComposite`, `Composite` and `DoneComposite` callbacks for `PictOpSrc`.
It accepts native `x8b8g8r8`/`x1b5g5r5` pictures, no mask, alpha map, repeat or
transform, and nearest filtering. Source bounds must already be clipped. The
callback fences Mesa's queued work, submits two GS textured triangles and
restores the previous color/depth binding. Attach it after the owned EXA/render
bridge and before `exaDriverInit`. Further operators, alpha glyphs, a complete
DDX and VT/input/cursor setup remain work. All shared submission is single-threaded.

Eleven new host ASan/UBSan and R5900 Linux cases pass. They exercise the actual
packet layouts, provider, sampler views and atomic resource references, final
draw callback, EXA callbacks and binding restoration, upload ordering, and the
actual probe's success/hardware-error/SIGINT paths. Clean application and exact
source equality pass for `ps2-gs-textures.patch`, `mesa-ps2-textures.patch` and
`xorg-ps2-textures.patch`, after their rendering patches.

It retains 576p, ext4, GNU tools, SMAP, UK keys and `root` / `eelinux`, adds the fifth
startup probe, and preserves the earlier image. To repeat the new probe:

```sh
ps2-gs-texture-test
```

Expect a square with red/green/blue/white bars and checkerboard on the left.
The right panel is a perspective trapezoid with blue/green/red/white bars and
checkerboard, drawn after updating the same texture. The earlier left panel
must retain its original colors. The text console returns after four seconds.
Startup output is saved to `/run/ps2-gs-texture.log`. The controlled PCSX2 log
records all five probes completing, native texture submission passing, SMAP
link-up and no panic. Completion does not qualify the sampled pixels; visual
results are recorded separately in [texture evidence](graphics-texture-validation.json).
The probe uses Mesa components, not an OpenGL application or complete X desktop.

The user confirmed both panels and text restoration in the interactive
`pcsx2-gs-texture-view` session. Their screenshot also exposed a diagnostic
geometry problem: a 120-pixel top edge and 184-pixel bottom edge were paired
with reciprocal W values of 0.5 and 1. Those ratios do not describe a planar
rectangle and kink the texture scale across the shared triangle edge.
The corrected diagnostic uses a 92-pixel top edge, matching the depth ratio.
The independent projection check fails with the previous geometry and passes
with the correction at nine interior points in each triangle. All three host
sanitizer diagnostic cases and eleven R5900 component cases pass.

Its kernel is byte-identical to the previous texture image; rendering component sources
are unchanged. The previous image and source snapshot are preserved. The user reports
that the corrected scene is better, and the attached PCSX2 screenshot shows continuous
checker lines. The user explicitly confirms the grid is continuous after replaying the
corrected probe. Perspective still makes the cells change size and lean; nearest
filtering of the 32x32 texture retains blocky edges. The 2026-10-04 hardware follow-up
also confirms the texture diagnostic on the real PS2 and supplies both panels. Pixel
readback, other sampler states and a working OpenGL/Xorg desktop remain open. See
[corrected projection evidence](graphics-texture-perspective-validation.json).

Packaging refuses to overwrite an existing image. The kernel source/output,
root staging, components and emulator session are separate from the preceding
candidate. Mesa shader/state execution, GLX, a complete Xorg DDX and existing
OpenGL application tests remain required before `startx` can use an accelerated desktop.

## Gallium context callbacks and Xorg lifetime

`mesa/ps2/ps2_pipe.c` supplies actual `pipe_screen` and `pipe_context` objects
against Mesa 24.0.9 headers. Resource creation/destruction, surface and sampler
view references, framebuffer binding, clears, texture uploads, native copies
and synchronous flushes connect to the existing owned GS endpoint. Sampler
state is copied at binding, so deleting a state object cannot leave a dangling
pointer. Color/depth target changes fence earlier work; an unchanged target
keeps native clear/triangle commands in the same batch. Xorg changes to the
shared target are detected before the next Mesa submission.

Screen/context destruction is deferred while referenced resources, surfaces,
views or Xorg leases remain alive. `desktop/xorg/ps2_context_bridge.c` holds
a context lease while EXA callbacks can reach it. Install its zero-initialized,
screen-owned structure after surface preparation and before texture preparation
and `exaDriverInit`. Cleanup restores the entire original callback table before
releasing the lease, including removing texture callbacks installed afterward.
It is idempotent. Additional callback owners must not be inserted while this
bridge is attached. A borrowed GS client must outlive the screen and all its
resources/leases; the standalone factory instead owns the file until final release.

This foundation supports one context/submission thread per screen, native
CT16/CT32 RGB and Z16 resources, one color target and bounded nearest/clamp
sampling. Depth clear accepts 1.0, encoded as zero for reversed GS depth.
The triangle adapter receives already transformed/clipped window coordinates.
Shader capabilities remain zero and `draw_vbo` explicitly rejects unfinished
shader execution. CPU texture mapping/readback, general alpha, other shader
states and shareable asynchronous fences are not implemented. These callbacks
do not advertise an OpenGL profile or install a software pixel renderer.

Six host ASan/UBSan cases cover the actual context/resource/reference callbacks,
Xorg callback restoration, failure handling and unsupported draws. Three more
execute the actual diagnostic, including independent planar texture checks,
hardware-error recovery and SIGINT cleanup. All nine also pass as R5900 Linux
programs with real GNU atomic references and wrapped GPU system calls.
The generic `mesa-ps2-contexts.patch` and `xorg-ps2-contexts.patch` apply after
the respective texture patches; source-release checks, clean series application
and exact source equality pass. See [context evidence](graphics-context-validation.json).

Its kernel is byte-identical to the qualified texture candidate. It retains 576p, ext4,
GNU tools, UK keys, SMAP and `root` / `eelinux`, and adds a sixth startup probe using
the real context callbacks. To replay it after login:

```sh
ps2-gs-context-test
```

Expect original red/green/blue/white bars and a checker rectangle on the left,
then an updated blue/green/red/white perspective grid on the right, followed by
the text console. The context probe shares the corrected texture scene, so
its pixels alone do not distinguish it from the preceding texture probe.
The PCSX2 log records the context probe exit as zero, all six command checks
passing, SMAP link-up and no panic/user segfault. The user supplied an expected
automatically displayed boot pattern; its exact probe identity is unspecified.
The 2026-10-04 all-tests hardware report confirms the context diagnostic on
the real PS2. Its panel pattern is shared with the texture probe, so the
the hardware verification report identifies this diagnostic by user confirmation. Full Xorg and existing
OpenGL applications remain untested.

Packaging refuses to overwrite an existing image and preserves the earlier
qualified images. Full DDX screen/GC/cursor/VT integration and Mesa shader,
presentation and GLX integration remain required.

## Native Xorg core bitmap glyphs

`desktop/xorg/ps2_glyph.c` now implements actual `PolyGlyphBlt` and
`ImageGlyphBlt` GC callbacks against the pinned Xorg headers. It uses signed
glyph bearings and advances, font ascent/descent, server-format bitmap padding,
drawable translation and the GC composite clip. Transparent text accepts
solid `GXcopy` with a full plane mask. Image text uses copy/solid semantics
regardless of the GC ALU/fill style, including a background for negative
advances. Unsupported formats, masks or states stop the operation without
calling a software pixel renderer.

The shared `graphics/ps2_gs_bitmap.c` helper decodes existing monochrome font
bits into clipped horizontal native GS fills. It validates and plans the
whole request before touching the GPU, fences earlier Mesa work, binds the
destination and restores the saved color/depth target after completion.
A hardware error stops further submission and prevents replay. CPU work
builds commands; it does not rasterize destination pixels or map a framebuffer.
EXA sync and Xorg damage notifications follow successful drawing.

A DDX must register the glyph private during setup, before the first server
GC allocation, using `ps2_glyph_register_private`. Xorg cannot enlarge GC
private storage after GC objects exist. A zero-initialized GC-owned
`ps2_glyph_gc` must be attached after GC validation
and EXA setup, and detached before GC validation or destruction. Detach restores
the original GC operation table and clears its private before releasing the
Mesa context lease. No additional GC operation wrapper may be inserted while
attached. A closing context cannot draw even while the lease keeps its memory
alive. The lifecycle component below now manages these hooks; the complete
DDX still needs to install that component.

Eight host ASan/UBSan cases exercise the real callbacks, bit order, padded odd
widths, preflight rejection, background extents, multiple clip boxes, owned
offscreen pixmaps, context lifetime and hardware errors. Three more execute
the actual standalone probe through success, failure and SIGINT cleanup.
All eleven pass as R5900 Linux programs with real GNU atomic references and
pixman region bookkeeping. Pixel references and GPU syscall wrappers are
test scaffolding, not production rasterization. The pinned Xorg build disables
Composite; redirected-window handling is not qualified. Render alpha glyphs
are still unimplemented.

`mesa-ps2-bitmaps.patch` and `xorg-ps2-glyphs.patch` apply after their respective
context patches. Source-release checks, clean series application and exact built-source
equality pass. It preserves the earlier images and the byte-identical kernel, and adds a
seventh startup probe. To replay it after login:

```sh
ps2-gs-glyph-test
```

Expect three rows spelling `PLAYSTATION 2` on navy: transparent cyan text,
white text on blue, and cyan text with a gap from window clipping. The gap
deliberately hides `S` and the following `T`, leaving `PLAY  ATION 2`.
The console should return. The controlled PCSX2 log records all seven
command/fence checks and glyph probe exit zero, followed by SMAP link-up,
with no kernel panic or user segfault. The user supplied the matching three-row
screenshot and asked about the missing `S`; the intentional clip was explained.
That initial screenshot assessment did not establish physical execution or
console return. The 2026-10-04 all-tests report now confirms the glyph
diagnostic on the real PS2, according to the verification report; further formats
remain unqualified. The controlled emulator process stops after
150 seconds; its timeout is separate from the successful probe exit.
This executable supplies minimal Xorg bookkeeping around the
real callbacks; it is not an X server or TWM session. Command/fence completion
does not establish pixel correctness. See [glyph evidence](graphics-glyph-validation.json).

This keeps its qualified image and source available while the new GC patch adds explicit
early private registration to the current glyph component.

## Xorg GC lifecycle and ordinary core text

`desktop/xorg/ps2_gc.c` wraps screen `CreateGC`/`CloseScreen` and all seven
`GCFuncs`. It registers every native GC private before allocation, detaches
glyph hooks before lower validation/state changes, captures replacement
callback tables, and reinstalls native operations after validation. GC copying
and clip changes retain the destination's own owner; destruction unlinks its
private before lower code can release GC storage.

Install a zero-initialized, screen-owned `ps2_gc_screen` after EXA/damage setup
and before the first server GC allocation. It keeps a screen context lease,
with additional leases for validated glyph GCs. Cleanup restores screen/GC
callbacks and clears privates before releasing leases or calling lower
`CloseScreen`. Other screen callback owners must unwrap before explicit cleanup.
Use one submission thread. Allocation is bounded to 1024 managed GCs; excess
creation returns failure before calling the lower allocator.

Ordinary `PolyText8`, `PolyText16`, `ImageText8` and `ImageText16` requests now
perform bounded core-font lookup and invoke the native glyph operations.
The encoding follows the font's row layout. Count, returned glyph count,
null glyphs and signed advances are checked before drawing. This avoids
upstream damage text wrappers temporarily replacing the native glyph entry
with lower drawing operations. Other drawing operations dispatch through the
existing EXA/damage chain, preserving its return values, then restore the
native hooks even if the lower tables changed. Existing EXA rejection of CPU
destination access is retained; this layer adds no software pixel renderer
or support for previously rejected raster states.

Eight host ASan/UBSan cases use mutable lower-table fixtures to exercise
revalidation, ordinary text encodings, state/clip copying, non-text dispatch,
creation/allocation failures, GC limits, context closure and hardware failure.
Three more run the actual diagnostic through success, failure and SIGINT.
All eleven pass as R5900 Linux programs with real GNU atomic references.
The eleven existing glyph tests also pass after the registration API change.
The fixtures model upstream unwrap/rewrap contracts using real headers;
they do not execute a complete Xorg server or its font service.

`xorg-ps2-gc.patch` applies after `xorg-ps2-glyphs.patch`, including the small
glyph registration API change. Its prerequisites, application to the actual
pinned Xorg source release, clean series application and built-source equality
are checked. Mesa continues to supply the shared context lifetime and GS
queue; its shader capabilities remain zero and `draw_vbo` is unfinished.
See [GC integration evidence](graphics-gc-validation.json).

Its kernel is byte-identical to the previous candidate. It preserves 576p, ext4, GNU
tools, UK keys and SMAP, and adds an eighth startup diagnostic. Run `ps2-gs-gc-test`
after login to replay the three-row text scene. The first row uses 8-bit transparent
text; the second uses 16-bit image text with a background; the last uses 16-bit text
with a clip gap hiding `S`/`T`. The pattern matches the preceding glyph probe, so pixels
alone do not distinguish the two code paths. It remains a standalone diagnostic with
minimal server bookkeeping, not an Xorg/TWM or OpenGL application session.

The controlled PCSX2 run passes all eight startup command/fence checks. The
GC probe exits zero at guest time 61.2983 seconds, followed by SMAP link-up.
No kernel panic or user SIGSEGV appears in the saved log. The launcher closes
this controlled run after 165 wall-clock seconds; its timeout is separate
from the successful diagnostic result. Authenticated login was not tested
in that run.

The supplied screenshot matches the two complete rows and the deliberately
clipped third row. Both `S` and `T` are hidden at the clip boundary, yielding
`PLAY  ATION 2`. Because the preceding glyph test uses the same pattern, the
screenshot alone cannot identify which diagnostic produced it; the saved
binary identities and GC log markers establish the new emulator code path.
The 2026-10-04 all-tests report now confirms the GC text diagnostic on the real
PS2, according to the hardware verification report; pixel readback and other
formats remain unqualified.

## Native solid Boolean operations and plane masks

`ps2-gs-logic.patch` extends the exclusive owned endpoint with
`PS2GS_HW_SOLID_LOGIC` and a typed forty-byte `PS2GS_SOLID_LOGIC` request.
It accepts an owned color handle (zero for front), a bounded rectangle,
constant native RGB color, RGB write mask and `GXclear` through `GXset` ALU.
RGB555 uses fifteen significant bits; RGB888 uses twenty-four. Alpha is
always preserved. Depth handles, invalid coordinates/ALUs, reserved fields
and noncanonical color/mask bits are rejected before any drawing. Existing
ownership, graphics-mode, stale-epoch and sticky failure checks remain active.

For each bit, evaluate the ALU with destination zero and one. Equal outputs
select a constant copy; outputs one/zero select inversion; zero/one preserve
the destination. The copy and inversion masks are disjoint, requiring at
most two native GS sprites. Inversion uses the GS blend equation with white
source, destination subtraction, fixed factor 128 and zero addition. This
derivation follows [PS2tek's GS blending and framebuffer-mask description](https://psi-rockin.github.io/ps2tek/)
and the actual kernel GS/GIF register definitions. Tests check raw register
words, including FIX at bits 32–39. RGB555 masks use exact pre-conversion bit
placement rather than scaled color values. Dithering, conditional blending
and forced alpha are disabled for this operation; depth writes are masked.
The packet restores an unrestricted context-2 frame mask before FINISH.

`desktop/xorg/ps2_logic_exa.c` wraps EXA `PrepareSolid`/`Solid` after the
context/texture bridge, before `exaDriverInit`. It handles all sixteen ALUs
and arbitrary native RGB plane masks for front and owned color pixmaps.
The component retains a context lease, fences queued Mesa work before
drawing, and restores the original callbacks before releasing its lease.
Clean it up before the outer context bridge. Other callback wrappers must
unwrap first; use one submission thread. Unsupported pixmap formats and
unavailable capability return failure, with existing CPU access rejection
retained. This supplies the drawing primitive for the separate native solid
GC routing described below.

`mesa/ps2/ps2_logic.c` provides an explicit native constant-source operation
on a real color resource or the front buffer. It validates resource ownership,
format and rectangle before flushing the shared queue. It retains the existing
color/depth binding and resource references. This adapter does not advertise
OpenGL logic operations or shader capabilities, and `draw_vbo` still rejects
unfinished shader execution. Neither component reads, maps or renders
destination pixels on the CPU.

Thirteen cases pass with host ASan/UBSan and as R5900 Linux programs:
four actual kernel packet/provider cases, six real EXA/Mesa adapter cases
and three actual diagnostic lifecycle cases. The independent test oracle
checks every source/destination byte combination for all sixteen ALUs,
then randomized full RGB555/RGB888 values and masks, including preserved
alpha. It decodes the real packet words; it is not GPU pixel readback.
Provider lock/syscall fixtures do not establish kernel lockdep behavior.
Failure and SIGINT cases verify GS release before restoring text mode.
The new kernel builds with `W=1`; two inherited framebuffer missing-prototype
warnings remain in unchanged functions.

It adds a ninth boot diagnostic and keeps the existing 576p, ext4, GNU, UK keyboard and
native SMAP configuration. Replay it with `ps2-gs-logic-test`. Expect sixteen solid
tiles in `GXclear`..`GXset` order, left-to-right and top-to-bottom. A white XOR outline
then appears and disappears after being drawn twice, followed by the text console. The
native source color is `0x56c3a9` and the initial destination is `0x957c2a` (GS byte
order). It is a standalone callback diagnostic, not a complete X server.

The controlled PCSX2 run completes all nine startup command checks and the
solid logic probe exits zero at guest time 71.5643 seconds. SMAP link-up
follows, with no kernel panic or user SIGSEGV in the saved log. The launcher
closes the controlled run after 185 wall-clock seconds; this timeout is
separate from the successful diagnostic result. The user supplies the
expected grid screenshot and confirms that the white outline appears,
disappears and returns to text. This visually qualifies that CT32 scene
during the PCSX2 run. The 2026-10-04 hardware report also confirms the logic
diagnostic and supplies the real hardware verification report. RGB555 and partial masks
still need GPU qualification. No authenticated login was tested in this emulator run.

Generic `mesa-ps2-logic.patch` and `xorg-ps2-logic.patch` apply after the
bitmap and GC series respectively; the kernel patch follows textures.
The exporters check prerequisite application on copies of the actual pinned
Mesa/Xorg releases, clean series application and exact source equality.
Previous qualified images and sources remain unchanged.
See [solid logic evidence](graphics-logic-validation.json).

## Native solid GC requests and Mesa batches

`desktop/xorg/ps2_solid.c` routes actual `FillSpans`, `PolyPoint`, `Polylines`,
`PolySegment`, `PolyRectangle` and `PolyFillRect` callbacks into native GS
Boolean drawing. It supports all sixteen constant-source ALUs, RGB plane
masks, solid fills and zero-width solid lines in all octants (the initial qualified solid stage supported horizontal/vertical lines). Points
use the foreground independently of fill style. Unsupported tiled fills,
wide lines, patterned dash fills and unsupported formats terminate the operation without CPU
pixel rendering. The later native fill override adds tiled FillSpans/PolyFillRect
while retaining these solid callbacks. The later dashed-line stage adds OnOffDash/DoubleDash
with FillSolid. This is a component for the future DDX; it does not install
an X server or start TWM.

Line planning handles `CapNotLast`, reverse directions, relative coordinates,
closed polyline corners and single-pixel degenerate segments. Adjacent outline
edges visit each corner once, so XOR outlines erase when redrawn. Collapsed
rectangles visit their pixels once. Separate overlapping primitives retain
request order and may touch the same pixel repeatedly. These distinctions
follow the [Xlib line/rectangle drawing semantics](https://www.x.org/releases/current/doc/libX11/libX11/libX11.html).
The rectangle path also retains `miPolyRectangle`'s signed endpoint saturation.
`FillSpans` receives coordinates already translated by its DDX caller; the
other requests use drawable-relative coordinates and the GC translation flag.
Canonical composite clip boxes are intersected with the native surface bounds.
Redirected-pixmap origins have a conditional Composite path; the diagnostic
build has that extension disabled, so it does not qualify a composited desktop.

The complete request is planned before any queue flush or native drawing.
Limits are 4096 input primitives, 256 clip boxes and 16384 clipped rectangles.
An unsupported later segment, invalid span width, allocation failure or budget
overflow rejects the whole request before submission. The rectangle payload
has a maximum of 256 KiB. Its largest realloc growth can transiently retain
384 KiB of old/new payload, excluding allocator metadata and other allocations.
This bound does not establish total Xorg/Mesa/TWM memory use; that measurement
still requires a working desktop on the 32 MiB console.

`mesa/ps2/ps2_solid_batch.c` validates the entire native rectangle array,
owned color handle, canonical native RGB bits and capability before fencing.
It holds a context lease for the complete batch, flushes previously queued
Mesa work once, and keeps color/depth bindings and resource references intact.
Hardware errors stop further rectangles and remain sticky in the shared GS
owner; they are not retried after possible drawing. Empty work, zero masks and
`GXnoop` do not flush or submit. This is an explicit native adapter, not an
OpenGL logic-op capability. Shader caps remain zero and `draw_vbo`/GLX are
unfinished. CPU bookkeeping plans rectangles; the GS renders the pixels.

Install a zero-initialized `ps2_solid_screen` after `ps2_gc_screen_prepare`.
An optional complete six-callback table is installed into existing validated
GCs and applied again after validation or lower EXA/damage table replacement.
Other GC operations and native text retain their existing routes. Successful
native operations mark EXA synchronization and append/process conservative
composite-clip damage. Cleanup restores the six lower dispatch callbacks and
screen `CloseScreen` before removing the private and releasing the lease.
The wrapped close then runs GC cleanup before lower screen closure. Explicit
cleanup must run before `ps2_gc_screen_cleanup`; other screen wrappers must
unwrap first. Submission remains on one thread.

Twenty-four unique cases pass host ASan/UBSan and R5900 Linux-user execution: ten solid
GC/Mesa batch cases, three actual solid diagnostic lifecycle cases, eight previous GC
lifecycle cases and three previous text diagnostic cases. Independent pixel references
check all ALUs/masks, clipping, points, line endpoint rules, degenerate rectangles, XOR
erasure and 256 randomized drawing sequences. GPU syscalls and lower server tables are
controlled fixtures in these tests; they are not a complete X server or GPU pixel
readback.

It keeps the earlier nine boot probes, kernel, ext4, GNU tools, UK keyboard, native SMAP
and 576p setup. Its tenth startup probe can be replayed with `ps2-gs-solid-test`. Expect
a cyan framed panel on navy, three white XOR outlines at successive positions each
appearing and erasing, then the text console. The diagnostic invokes all six actual GC
callbacks and the shared Mesa batch, using 41 solid commands across seven display waits.
It is not the TWM desktop. The controlled PCSX2 run completes all ten startup command
checks. The new probe exits zero at guest time 95.5014 seconds; SMAP link-up follows,
with no kernel panic or user SIGSEGV in the saved log. The controlled launcher closes
after its 240-second wall-clock limit; that process timeout is separate from the
successful diagnostic. The user supplies the expected panel screenshot and confirms all
three outlines erase and the text console returns. This visually qualifies that CT32
720x576 scene in PCSX2. The 2026-10-04 all-tests hardware report also confirms the solid
GC diagnostic on the real PS2 and confirms the expected panel in the real hardware
verification report. RGB555/partial-mask pixels remain unqualified; no authenticated
login was tested in this controlled emulator run.

`mesa-ps2-solid-batch.patch` and `xorg-ps2-solid-gc.patch` each follow their logic patch
and require kernel `PS2GS_HW_SOLID_LOGIC`. The exporter applies all prerequisites to
copies of pinned Mesa 24.0.9/Xorg 21.1.24, checks clean series application and compares
the resulting files to the build sources. It also cross-compiles the actual patched
component sources without project component include paths; the Xorg patch includes the
public Mesa batch header it consumes. It does not enable an incomplete DDX or Mesa
driver. Earlier qualified USB images are preserved. See [solid GC
evidence](graphics-solid-validation.json).

## Native Xorg CopyArea and shared Mesa copies

`desktop/xorg/ps2_copy.c` installs the actual GC `CopyArea` callback through
the existing GC lifecycle. Upstream Xorg `miDoCopy` performs source validation,
source/destination clipping and exposure bookkeeping. The native copy callback
converts the complete clipped region to the shared Mesa
`ps2_pipe_copy_boxes` API in `mesa/ps2/ps2_copy_batch.c`. It reports EXA sync and
conservative damage after success, retains mutable GC dispatch through
validation, and restores the copy hooks before solid/GC `CloseScreen` cleanup.

All boxes are checked before the shared drawing queue is flushed. A region is
limited to 256 canonical banded boxes sharing one translation and 1,048,576
copied pixels. Overlap uses `memmove` order across bands and boxes. The aligned
majority of each rectangle uses one native GS local transfer. Only the final
one to three pixels, when needed, use a 64x32 owned scratch surface and GS
texture triangles. The scratch allocation is 8 KiB of the separate GS memory
in either supported color format. No GS pixel readback or CPU destination
renderer is used. Live Mesa target/depth bindings and resource references
remain intact. Sticky hardware failure stops drawing and leaves any scratch
for file teardown, without replay.

The Xorg path supports `GXcopy` with the complete RGB plane mask, zero RGB mask,
and `GXnoop`. Other source-pixmap ALUs and partial copy masks remain work.
Formats are RGBX at depth 24 and RGB555 at depth 15; alpha storage is unused,
not a supported application alpha channel. This stage introduced an explicit
native Mesa adapter; its preserved diagnostic image retains the earlier
Gallium copy callback. The subsequent callback integration below removes its
aligned-width restriction. Shader/state execution, `draw_vbo`, GLX and full
DDX setup remain unfinished.

Twelve new host ASan/UBSan checks and thirty R5900 Linux-user checks pass. The
R5900 total includes nine copy cases, three actual diagnostic lifecycle cases,
ten solid regressions and eight GC/text regressions. Both builds link the
actual pinned Xorg `mi/micopy.c`. Independent source snapshots check exact
copied pixel positions for randomized overlaps, odd widths, edge reads,
RGB555 narrow surfaces, window and owned-pixmap clipping. Invalid batches and
capacity failures precede pending queue flush; device error, context leases,
GC table replacement, cleanup and signal handling are also checked. Wrapped
GS syscalls in these tests do not establish physical pixel correctness.

The generic patches `mesa-ps2-copy-batch.patch` and `xorg-ps2-copy.patch` follow
the solid batch/GC patches. Clean prerequisite application, source equality
and compilation of the patched components against the actual pinned release
copies pass without project component include paths. No new kernel ABI is
required. Earlier qualified binaries, source snapshots and USB images remain
preserved. See [copy evidence](graphics-copy-validation.json) and the
[patch guide](../userland/patches/PS2-graphics.md).

It retains the stable 576p kernel and previous ten diagnostic binaries, and adds an
eleventh startup check. After boot, `ps2-gs-copy-test` replays two identical
bars/checker panels, a down/right overlapping move of the right panel, and a final split
right panel with a narrow vertical clip gap. The text console must return. This
executable exercises the real GC callback and upstream clipping, using minimal server
bookkeeping; it is not an X server.

The dated all-ten real hardware report predates this new test. Its physical
verification is pending, and no new complete Xorg/TWM or OpenGL application
claim is made. The requested `pcsx2-console` session now uses this image; the
previous session is retained in its refresh backup. The requested emulator session completes all eleven startup command checks
and the copy probe exits zero at guest time 105.6865 seconds. The user confirms
matching initial panels, the down/right move, the final clip gap and console
return. The normal 300-second launcher timeout ended the session after these
checks; that timeout is not itself a boot result. No panic or user SIGSEGV
appears in the saved log. Real hardware verification of this new scene remains
pending.

## Actual Gallium resource copy integration

`mesa/ps2/ps2_pipe.c` now routes the real
`pipe_context.resource_copy_region` callback through `ps2_pipe_copy_boxes`.
It supports same-format RGBX/RGB555 2D color resources at level/layer zero,
with positive bounded source boxes of depth one. Ownership and format checks,
source bounds and unsigned destination bounds all precede submission. A bad
box or failed scratch allocation leaves previously queued drawing untouched.
Unsupported requests set the context's existing sticky error; device failure
is never replayed. There is no software destination renderer or pixel readback.

The callback now accepts odd widths, including a three-pixel RGB555 source.
It preserves live color/depth targets, sampler state and resource references,
and orders valid copies after queued Mesa drawing. The adapter also retains
its `memmove` overlap ordering for internal/Xorg callers; Gallium's documented
copy contract does not require overlapping blits. See the upstream
[Gallium context contract](https://docs.mesa3d.org/gallium/context.html#blitting).

Seven new ASan/UBSan cases and seven R5900 cases cover all widths from 1 to
137 in both color formats, narrow surfaces, eight overlap directions, eighteen
box/level/layer validation variants, ownership/format rejection, capacity
failure and sticky device errors. Thirty-nine regressions pass in each build,
including actual upstream Xorg CopyArea, solid/GC callbacks, Mesa context and
diagnostic cleanup. Wrapped GS calls and independent source snapshots verify
copy positions and callback state; they do not establish new physical pixels.
See [callback evidence](graphics-resource-copy-validation.json).

The incremental generic `mesa-ps2-resource-copy.patch` follows
`mesa-ps2-copy-batch.patch`. Clean prerequisite application, source equality and R5900
compilation against the pinned Mesa 24.0.9 release pass. Archives containing
`ps2_pipe.o` must also contain `ps2_copy_batch.o` now. Historical component records
validate their preserved source snapshots. The previous CopyArea USB image, kernel and
binaries are unchanged; this callback has not yet been qualified in PCSX2 or on real
hardware. No new USB image is packaged for this callback-only change.

For desktop completion, the remaining milestones are a native Xorg DDX with
screen/cursor/input/VT/presentation integration, Mesa shader/vertex/state
execution through `draw_vbo`, GLX context creation for supported existing
OpenGL applications, and a real `startx`/TWM session with idle and peak RAM
measurements. The themed TWM configuration and cross-built packages are
prepared; these driver and application milestones remain open.

## Main RAM budget

The minimal Xorg/Mesa/TWM desktop targets the PS2's 32 MiB of main RAM.
Its total and peak usage are **not verified** while the full DDX and OpenGL
path remain unfinished. The user's roughly 7 MiB console usage is a reported
baseline, not a measured desktop result. Adding the desktop will increase
usage; fitting the minimal desktop does not imply every OpenGL application
will fit. Kernel reservations also reduce the RAM available to processes.

The copy adapter uses fixed bounded arrays. Target GCC reports individual
stack frames of 6,192 bytes for the Xorg clipped callback, 6,320 bytes for the
Mesa region batch and 6,000 bytes for its isolated texture submission helper.
These frames can be nested; they do not measure the full server's stack,
process RSS, allocator costs or total desktop memory. Its only additional GS
allocation is the 8 KiB scratch surface for unaligned copies.

GS front buffers, textures and depth storage use the separate 4 MiB VRAM.
Programs, fonts, geometry, resource bookkeeping and upload/command buffers
still use main RAM. The current glyph helper allows at most 4096 glyphs,
256 clip boxes, 1048576 decoded source pixels and 32768 planned spans per
request. On o32, span storage is at most 384 KiB. During a moving `realloc`,
the old and new span blocks can briefly total 576 KiB; glyph job metadata
adds at most 112 KiB. These are payload bounds for this operation, excluding
allocator overhead, existing font bits and other process allocations. They
are not estimates of the desktop's total resident memory.

The 64 MiB ext4 root filesystem stays on USB; it is not loaded wholesale
into main RAM. Record `free -m` and `/proc/meminfo` at console idle, during
`startx`, at desktop idle with TWM/xterm, while a supported OpenGL application
runs, and after X exits. Measure peaks as well as steady usage on a 32 MiB
console with swap disabled. Per-process RSS includes shared library pages,
so adding RSS values alone can double-count memory. This acceptance check
remains open in [TODO](../TODO.md).

## Retest the depth-layout correction

Launch `mass:/EELINUX.ELF` in LaunchELF. Its ext4 root already contains the compatible
probe; it does not need replacing.

Its FAT32 boot partition and 64 MiB ext4 root are checked by the image assembler.
Assembly writes only a regular file, never a physical device.

Log in with `root` / `eelinux` and run:

```sh
ps2-gs-test
```

For four seconds, expect a navy background, two cyan rectangles, and one
**continuous** cyan triangle inside a larger red triangle. Red is submitted
last at a farther depth; cyan should remain in their overlap. There should be
no dark horizontal bands. The text console should return with a `PASSED`
command/fence result. Record the visual result separately: the executable
does not read pixels back from GS VRAM.

Host validation:

```sh
python3 tests/test_ps2_gs.py
```

The actual validator and packet functions run with ASan/UBSan: bounds,
unsupported states, context selection and CT16/Z16 versus CT32/Z16S are checked.
This does not replace emulator or physical execution.

## EXA copies through the shared Mesa GS adapter

`desktop/xorg/ps2_copy_exa.c` replaces EXA's three copy callbacks when attached.
The earlier surface callback calls the aligned-only local GS transfer directly;
this adapter routes each rectangle through `ps2_pipe_copy_boxes`, the same native
Mesa batch used by GC CopyArea and Gallium resource copies. RGBX/depth24 and
RGB555/depth15 copies now support odd-width tails and self-overlap. EXA supplies
clipped rectangles in the required order; the adapter preserves that stream and
chooses safe native transfer ordering within each rectangle.

`PrepareCopy` validates same-screen native storage, formats, directions, ALU
and RGB mask without flushing the shared queue. `Copy` revalidates both retained
pixmaps, their GS session/handles and signed bounds before flushing. Preflight
covers each rectangle, since EXA has not supplied the rest of the series yet.
Only GXcopy with a full RGB plane mask draws; GXnoop and zero RGB masks do
nothing. Empty and identity copies also preserve pending Mesa drawing. Other
copy ALUs/masks remain unsupported, and the existing native `PrepareAccess`
terminates CPU rasterization. This adapter adds no software pixel renderer.

Use zero-initialized, screen-owned `ps2_copy_exa` state. Install it after the
context/texture/logic EXA bridges and before `exaDriverInit`. Retain its source
and destination pixmaps through `DoneCopy`. Restore these callbacks before
cleaning the outer context bridge. The adapter holds a context lease, checks
cleanup ownership and permits a single submission thread. Each copy retains
the batch's bounded pixel limit and optional 8 KiB GS scratch surface.

Eight new ASan/UBSan cases and sixteen shared GC/Gallium copy regressions pass
on the host and in R5900 Linux-user execution. Tests check widths 1 through 137
in both formats, tiny RGB555 sources, front/offscreen copies, eight ordered
multi-rectangle overlap directions, seventeen malformed state/rectangle
variants, stale sources, registration/cleanup failures, context shutdown,
no-op ordering and sticky device failures. Independent pixel models check GS
copy/sampling positions; these are not GPU readback or a running X server.
See [EXA copy evidence](graphics-exa-copy-validation.json).

The generic `xorg-ps2-exa-copy.patch` follows `xorg-ps2-copy.patch` and uses Mesa's
existing `mesa-ps2-copy-batch.patch` API. Its full Xorg 21.1.24 prerequisite series
applies cleanly, matches the repository sources, and compiles using patched public
component headers. Earlier qualified images, binaries and the text-only real-hardware
report are preserved.

New EXA copy callback runtime pixels and full X server lifecycle remain
unqualified. The later native screen `CopyWindow` component uses the shared
Mesa copy batch directly; it does not execute these EXA copy callbacks. The full native DDX, Mesa shader/vertex pipeline, `draw_vbo`, GLX,
first `startx`/TWM session and real desktop RAM measurements are still pending.

## Gallium blits and shared EXA source composites

`mesa/ps2/ps2_blit.c` provides `ps2_pipe_blit_rect`, a public native rectangle
adapter used by the actual Gallium `blit` callback in `ps2_pipe.c` and the new
EXA composite adapter. Matching RGBX/CT32 and RGB555/CT16 resources support
nearest up/down/anisotropic scaling, horizontal/vertical source flips and
destination scissoring. Clipping retains the original source mapping.
Unscaled, nonflipped transfers use the existing native copy batch, including
odd-width tails and overlapping self-copies.

Scaled/flipped transfers emit two GS textured triangles. The source must be
an owned texture, distinct from the destination, at most 1024 by 1024 pixels;
scaled self-copies and front-buffer sampling are rejected. The destination
can be native front or owned storage. Geometry, storage, ownership and
supported states are checked before queued drawing is flushed. Empty regions
and unused-alpha/zero channel masks preserve the queue. Logical framebuffer,
depth and sampler bindings remain unchanged; no CPU destination rasterization
or pixel readback is introduced, and the kernel ABI is unchanged.

Gallium accepts level/layer zero, box depth one, matching formats, nearest
filtering and full RGB channels with optional unused alpha. Partial RGB masks,
linear filtering, alpha blending, depth/stencil, MSAA, format casting and
nonzero levels/layers remain unsupported. Conditional rendering/query state
is not implemented. Shader capabilities remain zero and `draw_vbo` still
rejects unsupported execution.

`desktop/xorg/ps2_blit_exa.c` installs `CheckComposite`, `PrepareComposite`,
`Composite` and `DoneComposite`. It accepts opaque `PictOpSrc` in matching
native picture layouts, with no mask, repeat, transform or alpha map and
nearest filtering. These Xorg copies are unscaled; the scaling/flips above
currently belong to Gallium. Each rectangle revalidates prepared pixmap
storage, bounds and the live GS session before invoking the shared adapter.
Unsupported picture states are rejected; native `PrepareAccess` continues
to prevent software pixel fallback.

Install zero-initialized, screen-owned `ps2_blit_exa` state after the context
and texture bridges, before `exaDriverInit`. Retain prepared pixmaps through
`DoneComposite`. Restore these four callbacks before cleaning the outer
context bridge; if a later wrapper owns these callbacks, remove it first.
The adapter holds a context lease and requires one submission thread. Copy
and Boolean EXA wrappers own separate callback sets.

Eleven new host ASan/UBSan cases and thirty copy/context regressions pass on
the host and in R5900 Linux-user execution. An independent rational reference
maps destination pixel centers to source texels and compares the entire
destination. Scaling tests cover both native formats; EXA composite pixel
tests use RGBX. Checks include cropped/flipped/scissored sources, odd-width
self-copies, seventeen geometry/storage variants, seventeen unsupported
state/owner variants, no-op ordering, retained bindings, context shutdown,
registration/cleanup failures and sticky device errors. These tests model
native operations; they do not execute GPU rasterization. See
[blit evidence](graphics-blit-validation.json).

Apply `mesa-ps2-blit.patch` after `mesa-ps2-resource-copy.patch` and
`xorg-ps2-blit.patch` after `xorg-ps2-exa-copy.patch`. Their complete prerequisite
series apply cleanly against Mesa 24.0.9 and Xorg 21.1.24, match repository sources and
compile using patched component headers.

`desktop/ps2-gs-blit-test.c` now runs the actual Gallium `blit` and
`resource_copy_region` callbacks and the newly installed EXA composite hooks. Four
additional diagnostic lifecycle cases pass under host sanitizers and R5900 Linux-user
execution: normal completion, failed Mesa blit, failed EXA composite and interruption
restore the console after releasing GS ownership.

PCSX2 logs the four scenes and probe exit zero. The user's five screenshots
match the expected panel sequence, recorded as a qualitative visual review
without adding repository images. The scenes are:

1. Large, medium and original-size color/checker copies through nearest blits.
2. Normal, horizontal, vertical and both-axis flipped copies in a four-panel grid.
3. Full left panel and a central scissor crop of the same mapping on the right.
4. Matching odd-width copies, followed by an overlapping EXA move on the right.

The final move intentionally leaves the original top/left edge pixels visible;
it does not erase the source rectangle. There are five display pauses because
the fourth scene has before/after frames. After login, `ps2-gs-blit-test` replays
the sequence; `--brief` shortens the pauses. See
[runtime evidence](graphics-blit-runtime-validation.json).

The runtime diagnostic uses RGBX/CT32. Physical PS2 and RGB555 runtime pixel
qualification remain pending. Scene review is not GPU readback or whole-screen
artifact qualification. Earlier images, archives and the text-only real
hardware report are preserved. Full native DDX screen/VT/input/cursor and
presentation integration, Mesa shader/vertex execution, GLX, the first
`startx`/TWM session and idle/peak RAM measurements remain pending.

Build the visible diagnostic and package its separate regular image with:

The recorded PCSX2 console session was refreshed from this image, preserving
its old disk/profile in the reported backup. It uses the existing project MMU
runtime and remains interactive. `record-ps2-blit-runtime.py` saves an immutable
log prefix through successful probe exit, then checks image/binary hashes and
the preserved earlier artifacts. Replaying an already booted emulator disk
requires a fresh preparation or refresh because guest writes change its hash.

## Mesa, RAM and EDID

`scripts/mesa.py fetch --work PATH` verifies the pinned official archive and
license. Fetching is not a driver build. Mesa Gallium/DRI, GLX and application
tests remain implementation work. Only verified capabilities may be advertised;
unsupported operations must not invoke softpipe, llvmpipe or swrast.

CPU shadow pages allocate on demand. Desktop RAM, drawables, uploads and GS
colour/depth/texture reservations still require measurements. A video timing
fitting VRAM does not establish that a depth buffer or desktop working set fits.
Do not infer 1080p 3D support from the video mode catalog.

Acceleration adds no DDC transport. The project has no provider that reads EDID
through the user's PS2-to-HDMI converter. Explicit `ps2-video auto` retains the
boot mode without valid EDID. The selected default is now 576p/50, confirmed
flicker-free by the user; interlaced PAL remains an explicit compatibility mode.
See [video modes](video-modes.md) and [physical display results](hardware-display-validation.json).

## Native screen window movement

`desktop/xorg/ps2_copy_window.c/h` installs the actual `ScreenRec.CopyWindow`
hook and a matching `CloseScreen` owner. It uses `ps2_pipe_copy_boxes` to draw
through the shared Mesa GS context. The saved lower callback is restored during
cleanup and never used to render pixels. This is the screen operation needed
for moving window contents; it is separate from GC `CopyArea`, EXA `Copy`, and
Gallium's resource copy callback.

The hook follows the geometry contract in pinned Xorg 21.1.24's `exaCopyWindow`
and `fbCopyWindow`: translate the original source region from the old window
position to the new one, intersect with `borderClip`, and copy within the
window's backing pixmap. COMPOSITE pixmap `screen_x/screen_y` are subtracted
from both source and destination coordinates. All translated coordinates must
fit Xorg's signed 16-bit region representation; overflow is rejected before
pixman can silently clip it. Native storage/session, format, geometry, box
count and pixel budget are checked before pending Mesa work is flushed. The
caller receives its translated source region after success, including empty
copies. Empty and identity copies preserve the pending queue.

The shared batch supports front storage and owned RGBX/CT32 or RGB555/CT16,
orders overlapping region bands, and handles odd-width edges using its existing
bounded GS scratch surface. It retains Mesa's framebuffer, depth and sampler
bindings and resource references. There is no CPU destination pixel renderer
or new kernel ABI. A device error terminates the native request without replay.

Install zero-initialized, screen-owned state after `exaDriverInit` and before
`DamageSetup`, including cursor setup that invokes Damage setup. Xorg's Damage
wrapper then owns notifications around this hook. Install the GC/solid/copy
owners later as specified by their contracts. On close, later wrappers must
unwrap first; the native hook restores its saved callbacks before releasing
its context lease and entering lower `CloseScreen`. Replacing an already
wrapped Damage hook would bypass notifications and is outside this contract.
All components use one submission thread. Keep the COMPOSITE settings of
record headers consistent across translation units.

Eleven new sanitizer and R5900 Linux-user cases compare complete destinations
against an independent immutable source snapshot, scanning region membership
in screen coordinates. They cover all eight overlap directions, multiple clip
bands, redirected origins, both color layouts, odd widths, translation
contracts, empty/identity queues, invalid/stale storage, overflow, capacity,
closed contexts, sticky errors, wrapper nesting and cleanup. The Damage test
models the pinned wrapper's behavior; it does not run the complete Damage
subsystem. Forty-one prior copy/blit/context regressions also pass in each
environment. The new R5900 production archives use COMPOSITE consistently,
`NDEBUG`, GNU atomic references, soft-float and no LL/SC. The new component also
compiles against the patched pinned release with COMPOSITE disabled.

Apply `xorg-ps2-copy-window.patch` after `xorg-ps2-blit.patch`. Its complete
prerequisite series applies cleanly and compiles using patched public headers. Prior
images and text-only physical verification retain their original scope. The new hook is
now packaged in the runtime diagnostic below; PCSX2 panels are user-confirmed; physical
pixels and execution through a complete X server remain unqualified. Full
DDX/VT/input/cursor/presentation, Mesa shaders and `draw_vbo`, GLX, the first TWM
session and desktop RAM measurements remain open.

### Window-copy runtime diagnostic

`desktop/ps2-gs-window-test.c` calls the installed native `ScreenRec.CopyWindow` hook.
The four RGBX/CT32 scenes are:

1. Identical cyan-bordered color/checker panels, with the left panel retained
   as a reference.
2. An overlapping down/right move of the right panel. Original top and left
   edges remain intentionally; `CopyWindow` copies contents without clearing
   the old location.
3. The upper source and a lower clipped copy, with a four-pixel vertical gap.
4. A move inside a 256?192 offscreen backing pixmap with a nonzero screen
   origin, presented beside the reference.

The probe uploads client pattern data through Mesa, uses the actual Gallium
resource copy callback to seed the offscreen pixmap, and draws destination
pixels through native GS transfers. Its minimal screen bookkeeping does not
start an X server, the Damage subsystem, TWM or GLX. It verifies the region
translation contract during moves, restores the screen hook before releasing
resources, closes the GS session, and returns the console to `KD_TEXT`.
Failures and SIGINT/SIGTERM/SIGHUP follow the same cleanup path. Four host
sanitizer/R5900 lifecycle cases cover success, move failure, presentation
failure and interruption, in addition to the preceding 52 component cases.

PCSX2 completed all four scene command checks and logged probe exit zero at
70.7132 guest seconds. Successful console restoration is part of the probe's
exit-zero condition. The session remains interactive; after login, run
`ps2-gs-window-test` to replay the four scenes. Each scene pauses for three
seconds according to the guest clock (`--brief` uses one). The user confirms all four scenes and console return; the four supplied
screenshots match identical panels, retained overlap edges, the vertical gap
and the redirected backing move. This is qualitative panel verification, not
GPU readback or whole-screen artifact qualification. Physical PS2/RGB555
pixels and full X server use remain open. See the
[text-only runtime evidence](graphics-copy-window-runtime-validation.json).
Earlier diagnostic images and all twelve earlier binaries remain unchanged;
only the new probe runs automatically in this image. The previous PCSX2
console session is retained in its refresh backup. No physical disk was written.

The runtime recorder saves an immutable log prefix through probe completion;
launching the emulator alone is not treated as a successful boot. As with other
disposable sessions, replaying a later boot requires preparation/refresh if the
guest changed the disk hash.

### Native solid window painting milestone

`desktop/xorg/ps2_paint_window.c/h` installs `ScreenRec.PaintWindow` and sends
solid pixels through `ps2_pipe_solid_rectangles`, the existing shared Mesa GS
Boolean batch. It follows the solid-paint semantics of pinned Xorg 21.1.24's
`miPaintWindow`. Nested `ParentRelative` backgrounds use the resolved ancestor's
color and inhibition state; `None` and inhibited backgrounds draw nothing.
The destination remains the original child window, clipped to its `clipList`.
Borders use the original window's border pixel and the backing pixmap bounds.

The incoming region stays unchanged in screen coordinates. Redirected
RGBX/CT32 and RGB555/CT16 backing pixmaps subtract their screen origin for GS
coordinates. Background Damage regions remain in screen space on the window;
border regions use pixmap-local coordinates on the backing pixmap, as the
upstream Damage API requires. Empty intersections leave queued Mesa work
untouched. All region, storage, ancestry and allocation checks precede the
shared queue fence. A plan supports at most 256 clipped boxes and allocates
at most 4096 bytes, freed on success and error. This is a component allocation
bound, not a measurement of total desktop RAM.

The hook retains a context lease and restores `PaintWindow`/`CloseScreen`
before releasing it. Later screen wrappers must unwrap first. It can be
installed after EXA and before later Damage/GC wrappers; Damage must be
initialized before painting. The saved lower painter is never called for
pixels, and no scratch GC or CPU destination rasterizer is used. This preserved
solid-only milestone rejected tiled painting. The later tiled adapter below
extends the current hook; the incomplete DDX remains disabled.

Eleven new ASan/UBSan host cases and eleven R5900 cases cover pixel-center
references, clipping, inherited state, both backing formats, exact notification
coordinates, no-op queues, malformed regions/ancestry, missing or stale storage,
allocation failure, reverse CopyWindow/GC closure, reinstallation and a device
failure after a partial paint with no replay. Twenty-seven related solid,
window-copy and context regressions also pass in each environment. These use
real pixman regions and GNU atomic references with wrapped GS syscalls;
private registration and Damage notification are component fixtures. They do
not execute the actual Damage subsystem or an X server.

The generic
[`xorg-ps2-paint-window.patch`](../userland/patches/xorg-ps2-paint-window.patch) applies
after `xorg-ps2-copy-window.patch`, with source equality and compilation against pinned
Xorg 21.1.24 checked with COMPOSITE both enabled and disabled. See [text-only
window-painting evidence](graphics-paint-window-validation.json). No new runtime image
was packaged in this milestone; new PCSX2/physical pixel qualification, full
DDX/VT/input/cursor/presentation, Mesa shaders/`draw_vbo`/GLX and a measured
Xorg/Mesa/TWM session remain open. Earlier diagnostic images and the ten-test real
hardware report retain their scope.

### Native tiled window painting

The current `PaintWindow` hook also accepts distinct owned RGBX/CT32 or
RGB555/CT16 tile pixmaps. Its explicit Mesa adapter, `ps2_pipe_tile_rectangles`
in `mesa/ps2/ps2_tile.c/h`, repeats native source pixels with GS texture packets
and local transfers. It supports non-power-of-two periods and signed tile
origins. The source tile stays unchanged; the live framebuffer, depth/sampler
bindings and resource references remain intact. No source readback or CPU
destination rasterizer is used. This adapter does not advertise Gallium
repeat sampler state, shaders or GLX support.

Inherited backgrounds use the resolved ancestor's tile and origin while
clipping to the original child's `clipList`. A tiled border retains the
original window's border pixmap, aligned to the resolved background ancestor's
origin, following pinned `miPaintWindow`. Both subtract redirected backing
origins for native coordinates; Damage regions retain the existing background
screen-space and border pixmap-local conventions. Empty intersections and
None/inhibition no-ops preserve pending work.

For small patterns, native texture draws seed an aligned period in a GS
scratch surface. Native local copies double the pattern across and down to a
block of at most 64x32 logical pixels, at most 8192 logical pixel bytes. The
block repeats in bounded batches of 64 texture triangles. Full-screen 1x1 and
2x2 patterns at 720x576 need at most ten output packets in the component test,
plus native scratch setup, rather than a packet per pixel. Larger patterns
sample directly. Scratch is reserved before flushing queued Mesa work and
released after success; sticky device failure leaves it to file teardown.

The entire request is checked before flush: storage owner/format, distinct
source/destination, all boxes and total work. Limits are 256 boxes, 1,048,576
pixels and 65,536 output pieces. Tile dimensions are at most 1024; GS VRAM
capacity still applies. The host box plan remains at most 4096 bytes; these
bounds do not measure complete desktop RAM. Alias/front-buffer tile sources,
incompatible formats and excessive work fail explicitly.

Thirteen new ASan/UBSan host and R5900 cases pass, alongside 38 solid-paint,
solid-GC, window-copy and context regressions per environment. Independent
modulo references check all destination pixels, source preservation, clipped
and redirected coordinates, nested inheritance, randomized periods/origins,
no-op queues, capacity/allocation failures, reverse closure and failures in
texture seeding, scratch expansion and later output without replay. Real
pixman regions and GNU atomic references are used with wrapped GS syscalls;
private and Damage bookkeeping remain component fixtures.

The generic [`mesa-ps2-tile.patch`](../userland/patches/mesa-ps2-tile.patch) follows
`mesa-ps2-blit.patch`; the generic
[`xorg-ps2-tiled-paint.patch`](../userland/patches/xorg-ps2-tiled-paint.patch) follows
the preserved solid-paint patch. Both cleanly apply to their complete pinned series and
compile against Mesa 24.0.9/Xorg 21.1.24 with COMPOSITE enabled and disabled. See
[text-only tiled-painting evidence](graphics-tiled-paint-validation.json). The previous
solid milestone and older images/hardware evidence are preserved. Physical
qualification, actual X server/Damage use, full DDX and Mesa
shader/vertex/`draw_vbo`/GLX integration remain pending.

### Visible native window-painting diagnostic

`desktop/ps2-gs-paint-test.c` invokes the installed native `ScreenRec.PaintWindow`
and shared Mesa tile/solid adapters. The four scenes show a solid cyan fill with
a white border, repeated checker tiles with a cyan border, a continuous inherited
checker across adjacent child regions with a yellow/cyan non-power-of-two tile
border, and matching front/redirected backing-pixmap panels. The final scene paints
an owned 192x128 pixmap at a nonzero screen origin and presents it through Mesa's
native unscaled blit. Each scene pauses for three guest seconds.

Five additional host ASan/UBSan and R5900 checks cover success, solid-paint failure,
tile-expansion failure, presentation failure and SIGINT. An independent modulo
reference compares every modeled scene pixel; failures check session reclamation
and console restoration without replay. The executable uses minimal screen
bookkeeping and Damage callback stubs. Actual X server/Damage execution, RGB555
runtime pixels, physical qualification and a complete TWM/GLX session remain open.

Only the new probe runs automatically; all thirteen earlier diagnostic binaries remain
available. The stable 576p kernel is unchanged. After login, run `ps2-gs-paint-test` to
replay. PCSX2 completes all four scene command/fence checks with exit zero and text-mode
restoration in a 32 MiB guest. Supplied PCSX2 screenshots match scene 2's cyan bordered
checker, scene 3's continuous inherited checker and patterned border, and scene 4's
matching front/backing panels. The user also confirms scene 1's cyan fill with white
border and console return, completing qualitative scene verification. This does not
provide GPU pixel readback. See the [text-only runtime
evidence](graphics-tiled-paint-runtime-validation.json).

### Native tiled GC rectangle and span fills

`desktop/xorg/ps2_fill.c/h` installs actual `GCOps.FillSpans` and `PolyFillRect`
through the native GC override registry. FillSolid still delegates to the
existing native solid callbacks. Other solid operations, CopyArea and core
text/glyph routing keep their registrations when lower GC tables change or
ValidateGC runs. Install the fill owner after native GC/solid setup and unwrap
later screen wrappers before fill, solid and GC teardown; registrations retain
context leases until restoration.

Owned RGBX/CT32 and RGB555/CT16 tile pixmaps use the existing explicit Mesa
`ps2_pipe_tile_rectangles` adapter. It handles signed phases and non-power-of-two
periods without CPU destination drawing. Pattern origin is drawable origin plus
GC `patOrg`, minus the redirected backing origin. It does not depend on
`miTranslate`. FillSpans receives already-translated coordinates; PolyFillRect
translates drawable-relative rectangles when `miTranslate` is true. Both clip
against the GC composite clip and backing bounds. For `tileIsPixel`, the constant
source is `gc->tile.pixel`, with all sixteen native Boolean ALUs and RGB plane
masks. These contracts follow pinned Xorg 21.1.24 `fb/fbfill.c` and
`exa/exa_accel.c`, whose hashes are recorded with the component evidence.

Tile-pixmap writes currently require GXcopy with a full RGB mask. GXnoop and a
zero RGB mask preserve pending queue work. Front-buffer, aliased, foreign and
stale tile sources are rejected. Requests allow at most 4096 input spans or
rectangles, 256 clip boxes and 256 resulting paint boxes. The heap box plan is
at most 4096 bytes; all geometry is checked before the Mesa fence. Existing tile
limits are 1024 per source dimension, 1,048,576 cumulative pixels and 65,536
output pieces. Small patterns expand entirely on GS into at most 64x32 logical
pixels/8192 logical pixel bytes, subject to GS VRAM capacity, with at most 64
texture triangles per packet. Damage notification covers the GC composite clip,
matching existing solid-GC coverage; it is not an exact painted-footprint report.

Fifteen new host ASan/UBSan and R5900 cases plus 59 regressions pass per environment.
Independent modulo/Boolean references check destination pixels, unchanged source and
caller inputs, RGBX/RGB555 redirected coordinates, negative phases, rectangle/span
translation, all sixteen pixel-tile ALUs, queue/no-op ordering, GC revalidation, reverse
cleanup, allocation/capacity rejection and sticky seed/expansion/later-packet failures
without replay. Real pixman regions and GNU atomic references are used; private and
Damage bookkeeping remain fixtures. The generic [Xorg GC fill
patch](../userland/patches/xorg-ps2-tiled-gc.patch) follows
`xorg-ps2-tiled-paint.patch`, reusing `mesa-ps2-tile.patch`. The complete Xorg series
applies cleanly and these components compile against the pinned release with COMPOSITE
enabled and disabled. See [text-only tiled GC
evidence](graphics-tiled-gc-validation.json).

The separate `desktop/ps2-gs-fill-test.c` uses actual native CreateGC, ChangeGC,
ValidateGC and rectangle/span callbacks with minimal screen/Damage scaffolding.
Its four scenes show matching checker rectangle/span panels, a shifted checker
with a deliberate 12-pixel central clip gap, matching yellow/cyan 21x15 tile
panels, and matching front/redirected backing checker panels with a cyan
pixel-tile bar. Each pauses three guest seconds. Five additional host and R5900
lifecycle checks compare every modeled scene pixel and exercise seed, expansion,
presentation and SIGINT cleanup. These modeled pixels are separate from GPU
pixel qualification.

Only the new fill probe runs at boot; fourteen preceding diagnostic binaries and older
images remain intact, with the unchanged stable-576p kernel. Run `ps2-gs-fill-test`
after login to replay. New physical qualification, RGB555 runtime pixels, actual
Damage/server execution, stippled fills, full DDX and Mesa shader/vertex/draw_vbo/ GLX
integration remain pending. Complete Xorg/Mesa/TWM RAM has not been measured. The
earlier real-PS2 report and qualified painting probe retain their original scope and
exact archived sources. PCSX2 completes all four new fill scene command/fence checks
with exit zero and text-mode restoration in a 32 MiB guest. See [text-only fill runtime
evidence](graphics-tiled-gc-runtime-validation.json) for the exact binary, image,
session and completion log. The four supplied PCSX2 panels show matching rectangle/span
checkers, the shifted checker with intentional central gap, matching yellow/cyan
patterns and matching front/backing checkers with the cyan bar. The user confirms all
four scenes and console return. This qualitative verification does not provide GPU
readback or physical qualification.

### Native opaque stippled GC fills

`FillOpaqueStippled` now shares the installed FillSpans/PolyFillRect callbacks in
`desktop/xorg/ps2_fill.c/h`. Foreground and background come from the GC regardless
of tileIsPixel. The source must already be a valid server-format, CPU-resident
1bpp pixmap, at most 256x256, with a positive padded stride. Xorg uses
`BITMAP_BIT_ORDER`; Mesa's explicit adapter also accepts either bit order.
Drawable-relative `patOrg`, already-translated spans, miTranslate-dependent
rectangles, composite clipping and redirected backing offsets retain the tiled
fill contracts. GXcopy/full RGB masks are supported; GXnoop/zero RGB mask preserve
queued work. Transparent stipples and opaque non-copy ALUs/partial masks remain
unsupported. Depth-1 pixmap creation/drawing in the full DDX is still unfinished.

`ps2_pipe_opaque_stipple_rectangles` in `mesa/ps2/ps2_tile.c/h` snapshots only the
monochrome source rows, at most 8192 bytes. It reserves a temporary native GS
colour pattern and optional expansion block before flushing queued work. Native
Boolean fills write the background and foreground runs into that GS pattern;
there is no CPU colour-pixel expansion or CPU destination rasterizer. The existing
native tile engine seeds/expands small patterns, repeats non-power-of-two periods
and batches at most 64 texture triangles per packet. All geometry/work is checked
before allocation or flush. The unchanged limits are 256 paint boxes/4096 plan
bytes, 1,048,576 pixels and 65,536 output pieces. The colour pattern uses at most
262144 logical GS bytes and the expansion block at most 8192 logical GS bytes;
physical GS allocation/alignment and VRAM capacity still apply. Resources are
released on success or reclaimed by file closure after sticky device failure.
Large patterns can require many native seed commands; application performance
has not been measured. These bounds do not measure complete desktop RAM.

Fourteen new ASan/UBSan host and R5900 cases plus 74 regressions pass per environment.
Independent bit/modulo references check all destination pixels, unchanged source
and caller inputs, padded rows, both bit orders, negative origins, redirected
RGB555, all-zero/all-one/equal-colour patterns and GC revalidation. An injected
source change during the pending queue fence verifies that the earlier snapshot
is used. Invalid later geometry, allocation/GS capacity and seed/expansion/later
output failures check preflight, no Damage success notification and no replay.
The real Damage subsystem and X server are not executed by these fixtures.

The generic [Mesa stipple patch](../userland/patches/mesa-ps2-stipple.patch) follows
`mesa-ps2-tile.patch`; the [Xorg stipple
patch](../userland/patches/xorg-ps2-stippled-gc.patch) follows `xorg-ps2-tiled-gc.patch`
and includes the public Mesa header it consumes. Both complete patch series apply
cleanly and the modified components compile against pinned Mesa 24.0.9/Xorg 21.1.24 with
COMPOSITE enabled and disabled. Previously qualified fill/paint sources, reports and
images stay intact.

The separate `ps2-gs-stipple-test` uses actual native GC creation/revalidation
and callbacks with minimal screen/private/Damage scaffolding. Its four scenes
show matching blue/white rectangle/span patterns, a shifted blue/white checker
with an intentional central gap, matching yellow/cyan 21x15 patterns, and matching
front/backing blue/white panels with a green equal-colour stipple bar. All colour
patterns are constructed on GS; the probe uploads no colour-pixel arrays. Five
additional host/R5900 lifecycle cases compare every modeled scene pixel and test
native seed, expansion and presentation failure plus SIGINT cleanup.

Replay after login with `ps2-gs-stipple-test`. New physical qualification, RGB555
runtime pixels, actual X server/Damage, full DDX/Mesa/GLX and TWM RAM measurements
remain separate. This does not advertise shaders or new Gallium sampler capabilities and
is not a complete X server. PCSX2 completes all four opaque-stipple command/fence scenes
with exit zero and text-mode restoration in the 32 MiB guest. The exact run is recorded
in [text-only stipple runtime evidence](graphics-stippled-gc-runtime-validation.json).
All four supplied PCSX2 panels match the expected opaque-stipple scenes, including the
intentional central gap and final green equal-colour bar. The user confirms all four
scenes and console return. This qualitative verification is recorded without adding
images and does not provide GPU readback or real PS2 qualification.

### Owned source-only monochrome pixmaps

The Xorg EXA storage adapter now accepts depth-1, 1bpp pixmaps up to 2048x2048.
It owns zeroed bitmap memory padded with the server's `BitmapBytePad`, without
allocating a GS colour surface. Each screen has a 1 MiB source-pixel budget and
1024-pixmap limit, including empty headers. Metadata and other process memory
are additional; this is not a full desktop RAM measurement. Header dimensions,
format, stride and ownership are checked, and replacing/resizing an existing
bitmap allocation is rejected by the driver callback.

EXA stores a system pointer and hides `devPrivate.ptr` between accesses. The new
`ps2_surface_exa_mono_source` view validates the registered owned allocation and
allows native GC fills to read it while the pointer is hidden. Existing readable
server bitmap headers remain accepted. Mesa snapshots source bits before its
queue fence and builds/repeats the colour pattern on GS, using the previously
qualified opaque-stipple adapter. The native pattern still has a 256x256 limit.
No additional Gallium capability or GL shader support is advertised.

Depth-1 GCs preserve the lower EXA/fb editing and text callbacks, wrapping their
mutable tables and lifetime. Bitmap source bits can be edited in CPU memory;
colour GCs keep native operations and colour `PrepareAccess` remains fatal.
`UploadToScreen` does not claim monochrome acceleration: normal client bitmap
uploads rely on the lower EXA/fb path. Actual complete server/client execution
still requires the DDX. The component fixture models lower bitmap edits and
callback table replacement; it does not execute the full EXA/fb or Damage system.

Ten new cases plus 88 prior drawing regressions pass under host ASan/UBSan and
R5900 Linux-user emulation. Checks cover zeroed padding, count/byte budgets,
non-head unlink, both allocation failures, immutable/corrupt headers, hidden
source pointers, edited sources, RGBX/RGB555 output, monochrome GC rewrapping,
colour-access rejection, and rejection before the pending queue fence.
The generic `xorg-ps2-monochrome.patch` applies after `xorg-ps2-stippled-gc.patch`
and compiles against pinned Xorg 21.1.24 with COMPOSITE enabled and disabled.
See [component evidence](graphics-monochrome-validation.json). Earlier source
snapshots, reports, archives and hardware qualification remain unchanged.

The separate `ps2-gs-mono-test` allocates two source bitmaps through the actual
EXA driver callbacks, edits their bits through access hooks, hides their pointers,
and presents the same four native GS patterns as the opaque-stipple diagnostic:
blue/white rectangle versus spans, a shifted checker with an intentional gap,
yellow/cyan non-power-of-two patterns, and matching front/backing panels with a
green bar. Five host/R5900 probe cases cover modeled pixels, seed/expansion/
presentation failures and interruption cleanup. It preserves sixteen earlier
diagnostic binaries and the stable 576p kernel in a separate regular image.
PCSX2 completes all four command/fence scenes with exit zero and restores text
mode at 70.7039 guest seconds. Four supplied panels match the expected scenes,
and the user confirms all four and console return. This qualitative verification
is recorded as text only in [runtime evidence](graphics-monochrome-runtime-validation.json).
Images are not added or linked.

Replay with `ps2-gs-mono-test` after console login. New physical pixels,
tile-pixmap ALUs/masks, actual server editing and full DDX/Mesa/GLX/TWM
integration remain open. Transparent stipples follow below. Full application memory/performance is unmeasured.

### Transparent stippled GC fills

`FillStippled` now routes rectangle/span requests through
`ps2_pipe_stipple_rectangles`. One bits emit native GS fill rectangles; zero
bits preserve the existing destination. GXcopy/full RGB masks use packets of
64 native commands. Other Boolean ALUs and partial RGB masks use the existing
GS Boolean rectangle path. All 16 ALUs work in RGBX32 and RGB55516 component
checks, including overlapping requests in their original order. This adapter
adds no Gallium shader, sampler or OpenGL capability.

Mesa snapshots at most 8192 monochrome source bytes, merges consecutive equal
source rows and preflights the exact one-bit run count before flushing pending
work. Requests retain the 256 clipped-box, 4096-byte plan and 1048576-pixel
limits. GXcopy/full-mask output allows 262144 runs; Boolean/partial-mask output
allows 16384. Exceeding either limit rejects the operation before fencing.
An all-zero source emits nothing, keeps the queue and skips Damage. There is
no CPU destination renderer, colour-pixel expansion, readback or GS colour
pattern allocation in the transparent path. The compiled R5900 adapter stack
is 6320 bytes, its run emitter is 104 bytes, and the bitmap snapshot/GC plan
are additional; these figures do not establish total desktop memory use.

Sixteen new plus 98 regression cases pass under host ASan/UBSan and R5900 Linux-user
emulation. Independent pixels cover both bit orders, padded rows, negative/extreme
origins, clipping, redirected storage, all ALUs/masks, overlap order, snapshot timing,
no-ops and failure recovery without replay. The modeled full 720x576 2x2 checker emits
207360 rectangles in 3240 packets; fine-pattern performance on actual hardware remains
unmeasured. See [component evidence](graphics-transparent-validation.json).

Apply `mesa-ps2-transparent-stipple.patch` after `mesa-ps2-stipple.patch`, and
`xorg-ps2-transparent-stipple.patch` after `xorg-ps2-monochrome.patch`.
Both full generic series apply cleanly and the changed components compile
against pinned Mesa 24.0.9/Xorg 21.1.24 with COMPOSITE enabled and disabled.

The separate `ps2-gs-transparent-test` uses driver-owned hidden source bitmaps
and four visibly distinct scenes: matching red/white rectangle/span panels;
a shifted blue/cyan checker with a red clip gap; cyan/white partial-mask
patterns with a yellow bar restored by two XOR operations; and matching
orange/green front/backing panels with a magenta completion bar. Six lifecycle
cases per environment check every modeled pixel, first/later command failure,
Boolean-operation failure, presentation failure and interruption cleanup.
All colour drawing is native GS work. Minimal server bookkeeping and Damage
callback stubs remain; this probe does not start an X server.

Replay with `ps2-gs-transparent-test` after login. PCSX2 command/fence completion,
visual review, physical/RGB555 runtime pixels and full server execution are separate
qualification steps. Tile-pixmap ALUs/masks, DDX screen/input/cursor/VT/presentation,
Mesa shaders/vertex/draw_vbo/GLX, the first TWM session and total/peak RAM measurements
remain unfinished.

PCSX2 completes all four transparent-stipple command/fence scenes with exit zero
and text-mode restoration at 70.664 guest seconds. The exact binary, image,
session and immutable completion log are recorded in
[text-only runtime evidence](graphics-transparent-runtime-validation.json).
All four supplied panels match the expected scenes, and the user confirms all
four and console return. This qualitative verification stays text-only.
Physical/RGB555 runtime pixels, actual X server/Damage and full desktop
qualification remain pending.

### Opaque stipple Boolean operations and RGB masks, 2026-10-05

`FillOpaqueStippled` now supports all 16 Boolean ALUs and partial RGB plane
masks in the native GC rectangle/span hooks. General requests use
`ps2_pipe_opaque_stipple_logic_rectangles`, which partitions the source into
zero/one runs and applies a background or foreground operation once to each
covered destination pixel. Input order is preserved across overlaps, including
XOR. Colours equal under the active mask collapse to one rectangle per clipped
box. All-zero and all-one sources apply the background or foreground colour,
respectively. GXnoop/zero masks retain pending work and skip Damage.

The optimized GXcopy/full-mask path still constructs and repeats its colour
pattern on GS through the existing tile adapter. Other opaque states share
the bounded source-run machinery with transparent fills. A source snapshot of
at most 8192 bytes and exact run count precede the original queue fence;
requests retain the 256-box, 4096-byte plan and 1048576-pixel limits. General
Boolean/partial-mask requests allow 16384 zero/one rectangles. Both zero and
one runs count, so a 128x128 2x2 checker reaches that limit; 129x128 rejects
before fencing. No CPU destination pixels, colour expansion/readback or new
Gallium/OpenGL capability is introduced.

Sixteen new and 114 regression checks pass on host ASan/UBSan and R5900
Linux-user emulation. Independent per-pixel Boolean/modulo references cover
all ALUs/masks, clipped rectangle/spans, RGBX/RGB555 redirection, overlap order,
owned hidden sources, constant patterns, equal masked colours, source mutation
at the queue fence, both bit orders, extreme origins and sticky failure without
replay or successful Damage. Transparent and optimized opaque copy regressions
remain covered. The compiled shared run adapter frame is 6328 bytes, the opaque
wrapper is 72 bytes and the emitter is 104 bytes; these are local stack frames,
not total/peak application RAM. Fine-pattern runtime performance is unmeasured.
See [component evidence](graphics-opaque-stipple-logic-validation.json).

Apply `mesa-ps2-opaque-stipple-logic.patch` after `mesa-ps2-transparent-stipple.patch`,
and `xorg-ps2-opaque-stipple-logic.patch` after `xorg-ps2-transparent-stipple.patch`.
Both complete generic series apply cleanly and the changed components compile against
pinned Mesa 24.0.9/Xorg 21.1.24 with COMPOSITE enabled and disabled.

The distinct `ps2-gs-opaque-test` draws four scenes using driver-owned source
bitmaps: matching yellow/magenta opaque XOR rectangle/span panels; a shifted
yellow/magenta XOR checker with a blue clip gap; blue/magenta partial-mask
patterns with a cyan bar restored by applying opaque XOR twice; and matching
orange/cyan front/backing AND panels with a white completion bar. Six lifecycle
cases per environment check independent modeled pixels and cleanup after
first/later Boolean, bar, presentation and interruption failures. Colour drawing
remains native GS work, with minimal server bookkeeping and Damage callback
stubs; the probe does not start an X server.

Replay `ps2-gs-opaque-test` after console login. PCSX2 command/fence, visual,
physical/RGB555 and actual X server qualification remain separate. Tile-pixmap and
source-copy ALUs/masks, composite states, full DDX screen/input/cursor/VT/presentation,
Mesa shader/vertex/draw_vbo/ GLX, a TWM session and desktop idle/peak RAM measurements
remain unfinished.

PCSX2 completes all four opaque Boolean command/fence scenes with exit zero
and text-mode restoration at 109.3742 guest seconds. The exact binary, image,
session and immutable completion log are recorded in
[text-only runtime evidence](graphics-opaque-stipple-logic-runtime-validation.json).
All four panel scenes match the user-provided images; text-console return awaits user confirmation; physical/RGB555 runtime pixels,
actual X server/Damage, fine-pattern performance and full desktop RAM remain pending.

### Native zero-width lines in all octants

`desktop/xorg/ps2_solid.c` now routes diagonal `PolySegment` and `Polylines`
through the geometry-only `ps2_pipe_zero_line` adapter in
`mesa/ps2/ps2_solid_batch.c/h`. It honours the screen's Xorg zero-line bias,
all cap endpoint rules and closed/open polyline joins. Zero-width solid lines
use the foreground independently of the GC fill style. RGBX/RGB555, all sixteen
Boolean ALUs, RGB plane masks and redirected backing storage retain the shared
native solid submission path.

The CPU computes coverage and collects horizontal/vertical geometry runs.
GS Boolean rectangles perform colour destination operations. There is no CPU
colour framebuffer, destination mapping or software Mesa renderer. This stage
does not use direct GS line primitives. Closed-form Bresenham positions retain
phase across clipping; the planner skips huge offscreen portions and limits
iteration to the visible major-axis extent. Geometry supports the full signed
32-bit endpoint range without requiring 128-bit arithmetic on R5900.

Each complete GC request is clipped and validated before the existing Mesa
queue is fenced. Limits remain 4096 inputs, 256 composite region boxes and 16384
clipped runs, with at most 262144 bytes of heap geometry. Budget/allocation
failure leaves queued work untouched. Device failures remain sticky and report
no successful Damage or replay. Compiled R5900 frames are 168 bytes for the
geometry planner, 4272 for PolySegment and 4288 for Polylines; these figures do
not measure full desktop peak RAM.

Twelve new checks compare pixels against the actual pinned Xorg 21.1.24
`miZeroLine`/`miZeroClipLine` code and an independent Boolean reference.
They cover all octants, reversed endpoints, all 256 bias masks, drawable and
multibox clipping, caps/degenerates, XOR joins/erasure, fillStyle independence,
redirected RGB555, randomized requests, huge endpoints/CoordModePrevious,
work limits, allocation failure and later device failure. Together with 130
regressions, 142 checks pass under host ASan/UBSan and R5900 Linux-user execution.
See [component evidence](graphics-thin-lines-validation.json).

Apply `mesa-ps2-thin-lines.patch` after `mesa-ps2-opaque-stipple-logic.patch` and
`xorg-ps2-thin-lines.patch` after `xorg-ps2-opaque-stipple-logic.patch`. Source
equality, clean series application and pinned-release component compilation pass with
COMPOSITE enabled and disabled. Earlier qualification refers to each stage's preserved
source snapshot; its original recorder must not be rerun against changed ROOT sources.

`ps2-gs-line-test` shows matching cyan octant fans (point reference/native
segments); clipped yellow diagonals on blue with a preserved red gap;
matching white XOR diamonds with intact joins and a restored green bar; and
matching magenta front/backing diamonds with a white completion bar. Six
host/R5900 lifecycle checks validate every modeled scene pixel against Xorg's
reference, first/later failure, completion-bar failure, presentation failure
and interruption. Minimal server bookkeeping and Damage stubs remain in use.
The probe is not an X server, TWM session or OpenGL application.

Replay `ps2-gs-line-test` after console login. Visual/physical pixels, RGB555 runtime,
actual server/Damage, wide lines/arcs, patterned dash fills, tile/copy/composite states,
full DDX screen/input/cursor/ VT/presentation, Mesa shaders/vertex/draw_vbo/GLX, TWM and
total/peak desktop RAM remain separate requirements.

PCSX2 completes all four thin-line command/fence scenes with exit zero and
text-mode restoration at 70.0678 guest seconds. See
[text-only runtime evidence](graphics-thin-lines-runtime-validation.json).
The exact binary/image/session and completion log are preserved. All four
supplied line panels match, and the user explicitly confirms console return.
No images are copied or linked in the documentation. Physical/RGB555 pixels,
actual server/Damage, performance and full desktop use remain separate.

Older standalone probe fixtures supply the Xorg screen-bias symbol in their
test bookkeeping. Thirty-three additional host sanitizer lifecycle checks pass
against current production sources; see
[probe compatibility evidence](graphics-thin-lines-legacy-probe-validation.json).
These fixture-only changes have a separate snapshot. The qualified 142-case
component snapshot, archives and all saved diagnostic binaries remain unchanged.

### Native zero-width dashed lines

`ps2_solid.c` now supports `LineOnOffDash` and `LineDoubleDash` for
zero-width `PolySegment`, `Polylines` and `PolyRectangle` with `FillSolid`.
The shared `ps2_pipe_zero_dash` adapter retains the existing Bresenham bias
and computes geometry only. On/off gaps emit no destination operation;
double dash emits disjoint foreground/background runs. Native GS Boolean
rectangles apply all sixteen ALUs and partial RGB masks, including redirected
RGBX/RGB555 storage. No CPU colour framebuffer or destination access is added.

Dash phase uses distance along the original major axis, so drawable and
composite clipping preserve the pattern. Polylines continue phase across
joins, omitting shared endpoints; independent segments and each rectangle
restart at `dashOffset`. Rectangle corners and collapsed outlines retain the
existing endpoint rules. The GC snapshots both colours and at most 256
positive, even, DIX-normalized dash entries before submission. The geometry
API also accepts odd lists by doubling the effective period, and supports
full signed-32-bit endpoints and unsigned-64-bit offsets without overflow.

`ps2_pipe_solid_runs` validates every coloured run before one original queue
fence, retains the context lease for the batch and preserves submission order.
Limits are 4096 inputs, 256 clip boxes and 16384 runs; the heap plan is bounded
to 327680 bytes. Allocation, work-budget and invalid-state failures leave
queued work untouched. Native device failure stays sticky, with no successful
Damage or replay. Compiled R5900 frames are 440 bytes for geometry, 4552 for
PolySegment and 4568 for Polylines/PolyRectangle. These are individual frames,
not full desktop RAM measurements.

Sixteen new checks execute the unmodified pinned Xorg `fbSegment`/`fbBresDash`
and `miZeroClipLine` reference. They cover all octants and 256 biases, dash
offsets, multibox clipping, continuous joins, independent resets, rectangle
corners, caps and degenerate points, all Boolean ALUs/masks, redirected RGB555,
unsupported patterned fills, no-ops, complete preflight, work/allocation limits,
source mutation at the fence and sticky later failures. Together with 142
regressions, 158 component checks pass under host ASan/UBSan and R5900 Linux-user
execution. See [component evidence](graphics-thin-dashes-validation.json).

Apply `mesa-ps2-thin-dashes.patch` after `mesa-ps2-thin-lines.patch`, and
`xorg-ps2-thin-dashes.patch` after `xorg-ps2-thin-lines.patch`. Clean series
application, source equality and pinned-release compilation pass with COMPOSITE enabled
and disabled. Earlier qualified archives, patches, source snapshots, diagnostic
binaries, images and completion logs are preserved. Do not rerun an earlier recorder
against changed ROOT sources.

The new `ps2-gs-dash-test` shows matching cyan on/off dashed fans;
yellow/cyan double-dashed diagonals on blue with a preserved red clipping gap;
matching white/yellow double-dashed XOR diamonds with a restored green bar;
and matching magenta/cyan front/backing double-dashed diamonds with a white
completion bar. Six lifecycle checks per environment compare every modeled
scene pixel against Xorg's reference and exercise native failure, presentation
failure and interruption. Eight earlier standalone probes pass 39 additional
host sanitizer cases against current production; see
[compatibility evidence](graphics-thin-dashes-legacy-probe-validation.json).

PCSX2 completes all four command/fence scenes with exit zero and text-mode restoration
at 69.5018 guest seconds in a 32 MiB guest. Replay `ps2-gs-dash-test` after console
login. See [text-only runtime evidence](graphics-thin-dashes-runtime-validation.json).
All four supplied scene panels match, and the user explicitly confirms console return.
The probe uses minimal server bookkeeping and Damage stubs, and is not an X server or
OpenGL application. Physical/RGB555 pixels, wide lines/arcs, patterned dash fills,
further tile/ copy/composite states, complete DDX screen/input/cursor/VT/presentation,
Mesa shaders/vertex/draw_vbo/GLX, TWM and full desktop peak RAM remain unfinished.

### Native save-under monochrome pointer cursor

`mesa/ps2/ps2_cursor.c/h` adds a screen-owned 64x64 monochrome cursor with
native GS background capture, restore and coloured Boolean runs. Both source
and mask are snapshotted before the original queue fence; CPU RAM holds only
monochrome input and geometry. The colour destination and saved background
stay in GS storage. RGBX/CT32 and RGB555/CT16, both bitmap bit orders, hotspots,
edge clipping, empty masks, recolouring and hide/clear are modeled.

The shared surface client now observes front-buffer access after complete
request validation and before submission. Fill, logic, upload, queued render,
texture writes and front reads/copies synchronously remove a visible cursor.
Restoration uses an aligned GS copy with a recursion guard and does not flush
or replay the queue currently being submitted. A front-to-pixmap copy therefore
excludes the pointer image. Owned pixmap operations do not hide it. The next
cursor move or screen BlockHandler recaptures the changed background and draws
the desired image. These guards apply to the shared native client; the older
standalone command client has its own session and is not a desktop cursor path.

`desktop/xorg/ps2_cursor_screen.c/h` connects Xorg's real pointer sprite
callback types to the Mesa cursor. A screen-owned private retains the context
lease, copied pointer screen functions and lower CloseScreen/BlockHandler/
QueryBestSize callbacks. Install after native GC and Damage setup, with a
CPU-unmapped front pixmap. Raw Damage requests invalidation; the transport
guard handles native operations that report Damage after drawing. CloseScreen
removes Damage and releases the native cursor before the lower pointer/GC
callbacks. A failed GS restore still unwinds those lower callbacks. The
explicit suspend/resume entry point is ready for DDX VT hooks, which remain
unfinished. One pointer device is supported; ARGB or oversized cursors reject.

Sixteen cursor checks and 158 regressions pass under host ASan/UBSan and
R5900 Linux-user execution. The latest CloseScreen failure fix also passes a
focused repeat of the sixteen cursor sanitizer checks; this does not increase
the unique 174-case count. Tests independently check cursor pixels, saved
backgrounds, copy exclusion, queued rendering, input mutation, overflow,
allocation, RGB555 spare bits, ownership, device failure and callback cleanup.
Actual native callbacks execute, while the MI pointer and Damage framework
are test scaffolding. See [component evidence](graphics-native-cursor-validation.json).

The R5900 compiler measures the persistent cursor object at 83044 bytes,
including its 4096-run / 81920-byte plan and two 512-byte monochrome arrays.
This excludes allocator overhead. A 64x64 GS save-under surface uses 16384 bytes
in CT32 or 8192 bytes in CT16. The compiled R5900 set-cursor frame is 1128 bytes;
create uses 56 bytes and the planner 64 bytes. These are individual frames,
and neither they nor a 32 MiB emulator boot measure a full desktop's peak RAM.

Apply `mesa-ps2-native-cursor.patch` after `mesa-ps2-thin-dashes.patch` and
`xorg-ps2-native-cursor.patch` after `xorg-ps2-thin-dashes.patch`. Clean series
application, exact source equality and compilation against Mesa 24.0.9/Xorg 21.1.24 pass
with COMPOSITE enabled and disabled.

`ps2-gs-cursor-test` adds four distinct scenes: matching white/black arrows on
blue/cyan checkers, matching moved arrows with old locations restored, a
yellow/magenta front copy without cursor on the left and a live cursor on the
right, then clean green/cyan checkers and a white bar after suspend/resume and
cursor clearing. Each scene pauses ten seconds. Six independent pixel/lifecycle
checks pass per host/R5900 environment, including failures and interruption.
Nine preceding standalone probes pass 45 host sanitizer lifecycle checks; see
[compatibility evidence](graphics-native-cursor-legacy-probe-validation.json).

Replay `ps2-gs-cursor-test` after console login. This diagnostic supplies minimal MI
pointer/Damage bookkeeping and invokes native sprite callbacks; it does not execute an
input driver or a complete X server. Physical/RGB555, actual server/Damage/VT/input,
full DDX screen/presentation, Mesa shader/vertex/ `draw_vbo`/GLX, TWM and measured
desktop RAM remain separate qualifications.

PCSX2 completes all four cursor command/fence scenes with exit zero and text-mode
restoration at 168.4982 guest seconds in a 32 MiB guest. Exact binary/image/
session and completion log are preserved in
[text-only runtime evidence](graphics-native-cursor-runtime-validation.json).
All four user-provided panels match qualitatively. The small pixelated white
arrow with black outline is the intended monochrome shape; the left arrow in
scenes 1–2 is a native reference and the right arrow is the save-under cursor.
The left front copy in scene 3 correctly excludes cursor pixels; scene 4 clears
both panels. Explicit user confirmation of text-console return remains pending.
Input positions are scripted; USB mouse event handling is not tested here.

### Native client images and source spans

`desktop/xorg/ps2_image_gc.c/h` adds colour GC `PutImage` and `SetSpans`
callbacks through `mesa/ps2/ps2_image.c/h`. The GC registry installs both
callbacks in already-validated colour GCs and reinstalls them after lower GC
revalidation. Depth-1 GCs retain their existing source-only bitmap editing.
Install the image screen owner after native GC setup and before outer cursor
wrappers; unwind the cursor first and the image owner before lower GC cleanup.
Both owners retain context leases until their callbacks have been removed.

`PutImage` accepts native ZPixmap RGBX/CT32 or RGB555/CT16 client sources with
matching depth, zero left pad and dimensions up to 2048. XYBitmap/XYPixmap
support is recorded in the planar-image milestone below. Coordinates are drawable-relative even
when `miTranslate` is false, following pinned Xorg `fbPutImage`. `SetSpans`
uses already-translated points and independently padded source rows, following
`fbSetSpans`; widths and input order are preserved, including overlapping
spans. COMPOSITE window origins are removed before writing owned GS storage.
Complete clip and source-size validation precedes any queue fence.

Mesa snapshots the client source and ordered clipped boxes before submitting
queued rendering. Full-RGB-mask GXcopy uses native GS upload bands. Other
Boolean ALUs and partial RGB masks scan source colours into bounded horizontal
runs and use the existing GS Boolean operator. Colour destination pixels are
never read or drawn by the CPU. Empty clips, GXnoop and zero masks preserve
pending work; invalid input, allocation failure and excessive Boolean run
counts reject before the fence. Later device failure remains sticky without
replay or success Damage. Live render target/depth bindings are preserved.
Front writes go through the existing save-under cursor guard.

Source bytes are limited to 2 MiB, SetSpans inputs and clipped upload boxes to
4096, GC clips to 256 and output pixels to 524288. The Boolean path allows
16384 coloured runs; large full-copy images use uploads without that run limit.
The R5900 compiler measures boxes at 24 bytes and runs at 20 bytes. The worst
combined driver request allocation is 2621440 bytes, including the source
snapshot, frontend and Mesa box plans, and Boolean runs. This excludes allocator
overhead and the original client request buffer. Compiled individual frames
are 120 bytes for the Mesa image adapter, 128 bytes for PutImage and 152 bytes
for SetSpans; the existing upload packer has its separate 3072-byte buffer.
Total and peak Xorg/Mesa/TWM RAM remain unmeasured.

Sixteen new checks and 174 regressions pass under host ASan/UBSan and R5900
Linux-user execution. Independent pixels check clipping, both coordinate
contracts, padded RGB555 rows, overlap order, all sixteen ALUs, partial masks,
source/box mutation at the fence, allocation and work budgets, sticky upload
failure, GC revalidation, reverse cleanup and cursor overdraw. Actual native
callbacks execute with minimal server bookkeeping; MI pointer and Damage
frameworks remain stubbed. See [component evidence](graphics-client-images-validation.json).

Apply `mesa-ps2-client-images.patch` after `mesa-ps2-native-cursor.patch` and
`xorg-ps2-client-images.patch` after `xorg-ps2-native-cursor.patch`. Clean series
application, source equality and compilation against Mesa 24.0.9/Xorg 21.1.24 pass with
COMPOSITE enabled and disabled. Earlier qualified source snapshots, patches, binaries,
images and reports are preserved.

The distinct `ps2-gs-image-test` shows matching six-colour shaded bars through
PutImage and SetSpans, a shifted image clipped around a red central gap,
matching white/blue XOR checkers with a green bar, and matching front/backing
colour bars after cursor overdraw/restoration with a white bar. Each scene
pauses ten seconds. Six scene-pixel/lifecycle checks pass per host/R5900
environment, including upload/presentation failure and signal interruption.
Ten earlier standalone probes pass 51 host sanitizer lifecycle checks; see
[compatibility evidence](graphics-client-images-legacy-probe-validation.json).

Replay `ps2-gs-image-test` from the console. It uses a source-only client image buffer,
actual native GC callbacks and Mesa adapters with minimal MI/Damage scaffolding.
Physical/RGB555 and visual pixels, actual X server/input/VT/Damage, full DDX
screen/presentation, Mesa shaders/vertex/`draw_vbo`/GLX, TWM and total desktop RAM
remain separate qualification requirements.

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

### Native XYBitmap and XYPixmap images

The planar-image milestone extends `desktop/xorg/ps2_image_gc.c/h` with
XYBitmap/XYPixmap PutImage through `mesa/ps2/ps2_planar.c/h`. XYBitmap takes a
depth-1 source and maps its bits to the GC foreground/background. XYPixmap
requires the destination depth, 24 for RGBX/CT32 or 15 for RGB555/CT16. All
planes arrive in descending significance, with server bit order and independent
32-bit-padded rows. Every legal left pad from 0 through 31 is covered. The
pinned Xorg 21.1.24 `fbPutImage` and DIX request/reformat contracts are recorded
in [component evidence](graphics-planar-images-validation.json). PutImage stays
drawable-relative regardless of miTranslate; redirected storage origins are
removed after clipping. Existing ZPixmap/SetSpans and depth-1 source editing
retain their contracts.

The Mesa adapter snapshots source bits and ordered clipped boxes before
fencing queued work, then decodes source colours into horizontal geometry runs
for native GS colour fills on full RGB-mask GXcopy, and Boolean rectangles for
other ALUs or partial masks. Full copies leave spare bits unspecified, like
ZPixmap uploads; Boolean writes preserve those bits. All sixteen ALUs and
partial RGB masks are covered. The CPU neither reads nor draws colour destinations;
there is no intermediate colour-pixel expansion buffer. Front writes retain
save-under cursor removal. Noops, invalid input, allocation failures and run
budget rejection preserve queued work; late device failure is sticky without
replay or success Damage. The existing GC owner and reverse cleanup ordering
serve all image formats, with one submission thread and no new shader cap.

Limits are 2 MiB of source bytes, 2048 image dimensions, 2079 source bits per
row including left pad, 4096 clipped boxes, 256 clip rectangles, 524288 output
pixels and 16384 coloured runs for every XY request. R5900 boxes measure
24 bytes and Boolean runs 20 bytes. Maximum driver request allocations are
2621440 bytes: source snapshot, frontend and Mesa box plans, and native run
plan. Allocator overhead and the original client request buffer are excluded.
Individual compiled frames are 160 bytes for the Mesa planar adapter and
192 bytes each for PutImage/SetSpans. These measurements do not establish total
or peak Xorg/Mesa/TWM RAM within 32 MiB.

Sixteen new checks and 190 regressions pass under host ASan/UBSan and R5900
Linux-user execution. Independent destination pixels cover both source bit
orders, every left pad, RGB555, clipping and redirected origins, all ALUs and
RGB masks, source/header/box mutation at the original fence, request and heap
budgets, sticky failure, GC revalidation and cursor overdraw. Actual native
callbacks use minimal MI/Damage bookkeeping; real server execution is separate.

Apply `mesa-ps2-planar-images.patch` after `mesa-ps2-client-images.patch` and
`xorg-ps2-planar-images.patch` after `xorg-ps2-client-images.patch`. Clean series
application, source equality and pinned-release compilation pass in both COMPOSITE
modes. All earlier qualified snapshots, reports, patches, binaries and images retain
their original hashes.

`ps2-gs-planar-test` supplies a distinct four-scene diagnostic: matching
cyan/yellow diamond bands through XYBitmap and a ZPixmap reference; shifted
white/blue diagonal hatching with a red central clipping gap; matching
multicolour XOR steps through XYPixmap and a ZPixmap reference with a green
bar; and matching orange/cyan front/backing zigzags after cursor restoration
with a white bar. Each scene pauses ten seconds. Six independent modeled-pixel/
lifecycle checks pass per host/R5900 environment, including early/late device
failure, completion-bar/presentation failure, signal interruption and cleanup.
Eleven earlier probes pass 57 host sanitizer lifecycle checks; see
[compatibility evidence](graphics-planar-images-legacy-probe-validation.json).

The [initial visual run](graphics-planar-images-initial-visual-failure.json)
matched scenes 1-3, but scene 4 copied a black backing panel instead of the
orange/cyan zigzags. Command/fence exit zero did not qualify that run. Its
sources, archives, image and logs are preserved separately. The corrected
full RGB-mask GXcopy path uses complete GS colour fills. The new PCSX2 run
returns command/fence exit zero at 176.5307 guest seconds in 32 MiB. The user
confirms all four corrected scenes and normal console return; the supplied
final front/backing zigzags match qualitatively. See
[corrected runtime evidence](graphics-planar-images-runtime-validation.json).

Replay `ps2-gs-planar-test` from the console. The diagnostic uses source-only client
buffers and native GC/Mesa callbacks, with minimal MI/Damage scaffolding.
Physical/RGB555 pixels, actual X server/input/VT/Damage, full DDX, Mesa
shaders/vertex/draw_vbo/GLX, TWM and total desktop RAM remain separate qualifications.
No supplied images are stored in documentation.

### Native solid filled polygons

`desktop/xorg/ps2_polygon_gc.c/h` adds a native colour `FillPolygon` hook to
the existing GC owner. `mesa/ps2/ps2_polygon.c/h` scan-converts convex, concave
and self-intersecting contours with EvenOddRule or WindingRule. Both coordinate
modes honour miTranslate. Client points stay unchanged; translated server
vertices must fit signed 16-bit coordinates, with overflow rejected rather
than wrapped. Redirected storage origins are removed after translation.
Depth-1 source editing retains lower dispatch. Polygon tiles/stipples remain
unsupported by this new hook.

The Mesa adapter snapshots vertices and plans all clipped horizontal geometry
before the original queue fence. Exact integer edge intersections include
top/left edges and exclude bottom/right edges. In-place heap sort orders
crossings without library sorting allocations. Full RGB-mask GXcopy uses
native GS colour fills with unspecified spare bits; other ALUs/masks use
Boolean runs and preserve spare bits. No CPU colour destination, pixel
readback or colour expansion array is added. Existing cursor guards remove
the save-under cursor before front reads/writes. Late device errors stay
sticky, with no replay or successful Damage notification.

Requests are limited to 4096 vertices, 256 disjoint clipped rectangles,
4194304 edge tests and 16384 native runs. The maximum driver request heap
is 425984 bytes: frontend/backend vertex arrays, crossing array and run plan.
Allocator overhead and the original client request buffer are excluded.
R5900 points/crossings measure 8 bytes, clips 16 and runs 20; individual
compiled Mesa/Xorg polygon frames are 112/4216 bytes. These are component
bounds, not total or peak desktop RAM measurements.

Eighteen new checks and 206 regressions pass under host ASan/UBSan and R5900.
The independent pixel oracle executes pinned Xorg 21.1.24 MI's Bresenham and
active-edge polygon rasterizer, while the production adapter uses rational
edge intersections. Tests cover both fill rules, coordinate modes and formats,
all ALUs/masks, clipping/redirection, fence-time input mutation, noops,
allocation and work budgets, sticky failures, GC revalidation and cursor
restoration. See [component evidence](graphics-filled-polygons-validation.json).

Apply `mesa-ps2-filled-polygons.patch` after `mesa-ps2-planar-images.patch` and
`xorg-ps2-filled-polygons.patch` after `xorg-ps2-planar-images.patch`. Clean
application, source equality and pinned compilation pass in both COMPOSITE modes.

The distinct `ps2-gs-polygon-test` shows matching cyan concave arrows against
a ZPixmap reference; clipped yellow diamond halves over blue with a red gap;
a yellow even-odd ring on the left and solid winding rectangle on the right,
using white XOR over blue with a green bar; then matching magenta front/backing
stars after cursor restoration, with a white bar. The third scene deliberately
shows different fill-rule results. Six independent MI-pixel/lifecycle checks
pass per environment. The isolated image preserves 24 preceding diagnostics
and the stable kernel; replay `ps2-gs-polygon-test` from the console.

Actual server/input/VT/Damage, physical/RGB555 runtime pixels, polygon patterns,
fine-pattern performance, complete Mesa/GLX/TWM and total desktop RAM remain
separate work. Documentation retains written results only.

The new polygon probe exits zero at 229.5788 guest seconds in 32 MiB PCSX2;
see [runtime evidence](graphics-filled-polygons-runtime-validation.json).
All four supplied scenes match their expected results on qualitative review,
including the intentionally different fill-rule panels. The user confirms
normal text-console return. This acceptance uses no GPU readback or
pixel-by-pixel screenshot comparison. All 63 earlier host probe checks also
pass against the updated components; see
[legacy probe evidence](graphics-filled-polygons-legacy-probe-validation.json).

### Monochrome stippled polygons

The colour FillPolygon hook now accepts FillStippled and FillOpaqueStippled,
using `mesa/ps2/ps2_polygon_stipple.c/h`. The shared geometry planner produces
disjoint clipped scanline spans. Stipple metadata and up to 8192 source bytes
are snapshotted; the adapter partitions spans into foreground/background runs
before the original fence. Transparent zero bits preserve destination pixels.
Opaque zero bits use bgPixel exactly once. Pattern origin is drawable origin
plus patOrg, independent of miTranslate, with redirected storage origins removed.
Existing server 1bpp sources and hidden EXA-owned monochrome sources are supported.
Both fill rules and coordinate modes retain the solid polygon edge convention.

All sixteen Boolean ALUs and RGB masks are supported in RGBX/RGB555 models.
Full RGB GXcopy uses GS colour fills with unspecified spare bits; other ALUs
and masks use shared Boolean runs and preserve spare bits. Geometry is bounded
by 4096 vertices, 256 clips, 4194304 edge tests and 16384 spans. Pattern work is
bounded by 1048576 examined pixels and 16384 output runs; oversize requests
reject before drawing. No CPU destination mapping or colour-pixel expansion
array is added. Driver request heap is at most 696320 bytes (680 KiB), excluding
original client storage and allocator overhead. Compiled R5900 frames are 112
bytes for geometry, 72 for its spans wrapper, 152 for the stipple adapter and
4296 for the Xorg hook; these are individual frames, not total desktop RAM.

Eighteen new cases plus 224 regressions pass per host ASan/UBSan and R5900 environment
against independent pinned MI geometry and source-bit expectations; see [component
evidence](graphics-stippled-polygons-validation.json). Six independent diagnostic
pixel/lifecycle checks pass per environment. All 69 earlier host probe checks also pass;
see [legacy evidence](graphics-stippled-polygons-legacy-probe-validation.json). Apply
`mesa-ps2-stippled-polygons.patch` and `xorg-ps2-stippled-polygons.patch` after their
filled-polygon patches. Clean apply/equality and pinned compilation pass in both
COMPOSITE modes.

The distinct `ps2-gs-polygon-stipple-test` shows matching cyan/yellow checker
arrows against a ZPixmap reference; transparent yellow/blue checker diamond
halves with a red gap; a yellow/blue XOR ring on the left and filled rectangle
on the right with a green bar; then matching magenta/cyan front/backing checker
stars after cursor restoration with a white bar. The fill-rule difference in
scene three is intentional. Its isolated image preserves 25 preceding binaries
and the stable kernel. The new probe exits zero at 174.9583 guest seconds in
32 MiB PCSX2; all four supplied scenes pass qualitative review and the user
confirms normal text-console return. See
[runtime evidence](graphics-stippled-polygons-runtime-validation.json).
This acceptance uses no GPU readback or pixel-by-pixel screenshot comparison.
Tile-pixmap
polygon fills, physical/RGB555 runtime pixels, real Xorg/Damage/input/VT and
full OpenGL/GLX/TWM/RAM qualification remain separate work.

### Transformed 3D triangles and depth

`mesa/ps2/ps2_transform.c/h` adds an explicit opaque triangle-list API. A
row-major MVP transforms client positions, then seven homogeneous halfspaces
clip to `-w <= x,y,z <= w` and `w >= 2^-16`. Viewport conversion maps positive
NDC y upward and depth to the existing reversed GS Z16/GEQUAL convention.
The R5900 computes geometry with double intermediates; GS hardware rasterizes
colour and depth in local VRAM. Colours use native affine Gouraud interpolation.
This API accepts unit-range RGBA with alpha one and leaves Gallium shader caps
at zero. Shader execution, `draw_vbo`, GLX and VU programs remain unfinished.

The entire output list is prepared before target restoration, fencing or
submission. Invalid geometry and allocation failures preserve the original
queue; hardware failures remain sticky. Up to 256 input triangles clip to at
most 2048 output triangles, with a measured 196608-byte driver heap bound and
1624-byte R5900 transform frame, excluding allocator overhead and the client
array. Framebuffer/sampler/resource references survive shared Xorg operations.
Nineteen new plus 242 regression checks pass in host ASan/UBSan and R5900 Linux
user emulation; see [component evidence](graphics-transformed-triangles-validation.json).

The distinct `ps2-gs-transform-test` adds six lifecycle/pixel-model checks per
environment and four visible scenes: rotated RGB shaded triangles against an
independent window-coordinate reference; cyan near triangles over red far
triangles with yellow fully hidden, submitted in opposite orders; yellow
left/near clipped geometry against an independent reference; then matching
angled perspective colour cubes. Actual Xorg EXA Copy callbacks fence and
present both owned targets. Two RGBX colour/Z16 pairs consume 294912 bytes in
GS VRAM. CPU pixel models live only in test fixtures. The diagnostic uses
minimal server private bookkeeping, and full Xorg/Damage/input/VT and OpenGL
sessions remain separate qualification work.

Apply `mesa-ps2-transformed-triangles.patch` after the stippled-polygon Mesa patch.
Clean pinned-release application, source equality and compilation in both COMPOSITE
modes pass. The Xorg production source and patch series are unchanged. The probe exits
zero at 209.9709 guest seconds in 32 MiB PCSX2. All four supplied scene images pass
qualitative review, and the user confirms normal text-console return. These are
intentionally still scenes at fixed transformed angles; continuous animation is not part
of this diagnostic. See [runtime
evidence](graphics-transformed-triangles-runtime-validation.json). This acceptance
includes no GPU readback or pixel-by-pixel screenshot comparison.
