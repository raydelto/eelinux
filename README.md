# eeLinux

**eeLinux** is a GNU/Linux system project for the PlayStation 2. The **ee** in
its name comes from **Emotion Engine**, the console's CPU codename.

The target is **Linux 7.2.8**, a disk-backed GNU userland, usable Ethernet, and a
small Xorg desktop with TWM, within the console's 32 MiB of main RAM.
Kernel baseline and networking selections are combined in
[`configs/kernel.config`](configs/kernel.config), retaining their original settings.

The display default is 720×576 progressive at 50 Hz. The user confirms this removes
terminal flicker on the tested HDMI setup; interlaced PAL remains an explicit
compatibility option. Its explicit `720x576p@50` kernel mode is user-confirmed stable
from boot.

**This repository is an implementation in progress. The user confirms GNU
console boot, login, DHCP, external ping and DNS on an SCPH-90001. Xorg/TWM
packages now cross-build, and an experimental GS hardware command engine has
passed emulator tests. Native GS-only 2D has passed targeted emulator qualification;
Mesa OpenGL/GLX remains unfinished.
The architecture review gate remains closed.**

Revision r44 provides working triangle shade/unshade, square minimize,
circle maximize/restore and X close controls, with border and Alt+right-drag
resizing. Left- and right-click menus remain open after release and select entries
with a second click. Startup keeps the window manager responsive while wallpaper loads and
reuses an automatically invalidated application-menu cache. Expanded package
selections add **250 command names** over r43 across networking, administration,
storage, processes, archives and data tools; `ps2-tools`
browses the commands actually installed. See the
[desktop controls and rebuild guide](docs/desktop-controls.md) and
[userland guide](docs/userland.md).

The new [2 GB USB image](build/eelinux-images/20261106-r44/eelinux-pal-desktop-2gb-usb.img),
[Slim DVD](build/eelinux-images/20261106-r44/eelinux-pal-slim.iso),
[Fat 2.10/2.12 DVD](build/eelinux-images/20261106-r44/eelinux-pal-fat-2.10-2.12.iso),
[Fat 3.04 DVD](build/eelinux-images/20261106-r44/eelinux-pal-fat-3.04.iso) and
[SHA-256 checksums](build/eelinux-images/20261106-r44/SHA256SUMS) preserve the older images below.
The USB image includes a silent first-boot partition/ext4 expansion script,
which removes itself and its boot hook after success. Its source is
[`scripts/expand-usb-rootfs.sh`](scripts/expand-usb-rootfs.sh); DVDs exclude it.
The USB growth fix retains mounted-device write protection; the DVD kernel
clears loader-inherited PHY loopback before Ethernet negotiation.
The Buildroot staging warning file is removed from assembled roots.
[Automated checks](build/desktop-update-20261106/tests.json), final R5900
[window controls](build/desktop-update-20261106/final-twm-runtime.json),
[native SDK](build/desktop-update-20261106/native-sdk.json),
[command-line runtime](build/admin-work/composed-cli-runtime.json) and
[network runtime](build/network-work/runtime-smoke.json) checks pass.
Physical boot and physical startup timing remain unverified for this revision.

## Current GS hardware 2D

Revision r43 makes TWM Icon Manager entries match the window titles: `Clock`
and `Emotion Engine | Terminal` replace the applications' icon names. Entries
update and re-sort on title changes, including while minimized. The aligned
186-pixel panel, anchored growth and click-to-restore checks pass with GS-rendered
label readback. `xeyes`, `uxterm` and bitmap draw/save regression checks pass too.
The [r43 validation](build/graphics-applications-validation-20261018-r43/final-validation.json)
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

Use the [r43 2 GB USB image](build/eelinux-images/eelinux-pal-desktop-gs2d-r43-20261018-2gb-usb.img),
[checksum](build/eelinux-images/eelinux-pal-desktop-gs2d-r43-20261018-2gb-usb.img.sha256),
[native validation](build/graphics-applications-validation-20261018-r43/final-validation.json)
and [desktop capture](build/graphics-applications-validation-20261018-r43/native-v2/ps2-desktop-qa/desktop.png).
Normal login and `startx` remain; no long graphics tests run automatically.
The original working image, r40 and r41 are unchanged.

Driver sampling, conversion, filtering, blending and coverage use PS2 GS commands.
CPU/software raster fallback remains rejected; protocol, clipping geometry,
constants, command submission and raw byte transfers still use the CPU. Core
X11, 38 Direct XRender formats, 53 operator IDs and six filter names remain
advertised within documented device/request limits. Mesa OpenGL/GLX and
exhaustive physical rendering/performance qualification remain unfinished.

The prior r41 focused checks and physical responsiveness report remain in its
[revision record](build/graphics-hardware-2d-validation-20261018-r41/revision-record.json).
Its separate 24,905-check/39-session graphics run is tracked through
[live progress](build/graphics-hardware-2d-full-qualification-20261018-r41/progress.json);
that earlier driver run does not certify r42 or r43.

## Implemented

The desktop includes 70 current X.Org application/data releases from the existing
`x11r7` recipes, an XDG Applications menu and a classic analogue xclock. The full GNU
userland includes native GCC/G++, binutils, build tools, editors, GDB, Python and Perl,
with compiler headers and libraries preserved. Every DVD contains the GNU/Xorg files
directly under `/eelinux/root` in Rock Ridge ISO9660, and boots through native CDVD and
ISO9660 drivers. It contains no embedded USB image or disk swapfile. Writable state
lives in a RAM overlay with bounded compressed RAM swap; DVD size reflects the actual
system files. Eight-sector optical transfers and a prepared DVD menu reduce disc reads.
USB storage is not required for DVD boot. The supplied images use login `root` and
password `eelinux`. `make pal-media` creates the USB and DVD artifacts; individual
packaging targets are available. Fat compatibility depends on DVD Player firmware. GS
DMA batching reduces repeated waits during drawing. The native 2D backend
implements its advertised XRender formats and filters; GLX remains unfinished. Cursor restoration uses exact GS VRAM transfers to avoid resampling the
pixels beneath the pointer. The analogue clock has a movable TWM frame.

### Earlier graphics development

The paragraphs below preserve development milestones. Their remaining-work
statements apply to those revisions; the current 2D status and artifacts are above.

The new GS nearest UNORM8 Saturate compositor passes 8,192 native RGBA pixels,
and its Xorg dispatch passes host/R5900 models. Nearest packed Saturate now passes
56 native Xorg checks across four byte and ten word layouts using the new 128-handle
kernel. The older r6 matrix exposed an opaque-source rounding defect, fixed by GS
operator reduction in r7. Its broader 126-check matrix also passes, for 182
native nearest-Saturate Xorg checks. The r8 source adds full
16-fraction bilinear Saturate; its focused 12-check native matrix passes.
The r9 coverage extension passes 24 native Xorg checks for A1/A4/A8 destinations,
bilinear inputs, component-alpha masks and source/mask aliases. Its GS alpha
multiply/add/clamp path avoids RGB factors and division. The same diagnostic
takes about 210 rather than 545 emulated seconds, including Xorg startup/shutdown;
this is not a physical PS2 performance measurement. Unsupported requests still stop.
The r9 full host regression passes 274 cases across 15 suites; four focused
R5900 cases pass. Earlier native matrices below use their separately frozen modules.
The r10 extension preserves original RGBA16 solid-source and solid-mask colours
for Saturate, including scalar/component-alpha masks and A1/A4/A8 targets.
Its 24 focused native Xorg checks pass with PCSX2 CPU GS emulation. Host and
R5900 models each pass 960 solid scenarios and eleven metadata guards.
The helpers are part of the existing `.c` test files; the rebuilt native client
is byte-identical after this source reorganization.
The full host regression passes 278 cases across 15 suites. The four new cases
also pass again on host and R5900 after the test helpers were merged into `.c`.
See [the r10 solid record](build/graphics-hardware-2d-validation-20261015-r10/revision-record.json).
The r11 extension adds nearest/bilinear alpha-map Saturate. Original map alpha
precision is retained before filtering, and the parent and writable map encode
RGB/alpha independently from the same GS float result. All 48 focused native
Xorg checks pass under PCSX2 CPU GS emulation. Host and R5900 models each pass
592 mapped scenarios, including 22 target layouts, eight map layouts and map
input aliases. The 26 new primitive rejection checks also pass on R5900.
The r11 full host regression passes 287 cases across 15 suites; nine focused
R5900 cases pass. See [the r11 mapped record](build/graphics-hardware-2d-validation-20261015-r11/revision-record.json).
The r12 extension adds shifted same-format writable alpha-map aliases, using
GS scanline staging and per-store packed quantization. Its 12 focused native
Xorg checks pass with PCSX2 CPU GS emulation. The 1,610 new host/R5900 model
scenarios include 22 Saturate layouts and 71-pixel scanline boundaries.
The full host regression passes 292 cases across 15 suites; seven focused
R5900 cases pass. C changes remain in `.c` files. See
[the r12 alias record](build/graphics-hardware-2d-validation-20261015-r12/revision-record.json).
The r13 extension adds signed convolution Saturate on GS, retaining the
R5900 unsigned conversion after each tap and the final signed clamp. Its
12 focused native Xorg checks pass, including extreme coefficient overflow
followed by cancellation, varied destination alpha and shifted writable maps.
The full host regression passes 299 cases across 15 suites; 13 targeted
R5900 cases pass. The models verify 36,864 arithmetic float components and
180 convolution integration scenarios per environment. See
[the r13 convolution record](build/graphics-hardware-2d-validation-20261015-r13/revision-record.json).

The r14 extension synchronizes different byte/word-format writable maps
through GS raw backing, preserving scanline store order and observable padding.
Its 32 focused native Xorg checks pass under PCSX2 CPU GS emulation. Host
and R5900 models each pass 1,927 shared-backing comparisons across 120
ordered format pairs, plus nine rejection guards. The full host regression
passes 303 cases across 15 suites; seven targeted R5900 cases pass. Native
15-bit converted aliases, remaining formats/operators, full native matrices
and physical memory/performance qualification remain open. See
[the r14 alias record](build/graphics-hardware-2d-validation-20261015-r14/revision-record.json).

The r15 extension adds native depth-15 RGB555/RGB444 writable format
aliases. GS converts native CT16 storage to packed raw words, synchronizes
both decoded views in scanline store order, and transfers the final raw
result back to CT16. All 24 focused native Xorg checks pass under PCSX2
CPU GS emulation. Host and R5900 models each check every 15-bit raw value
in four layouts, 196 alias comparisons and 25 rejection guards. The full
host regression passes 308 cases across 15 suites; ten targeted R5900
cases pass. Broader native matrices, additional formats, gradients,
extended operators and physical memory/performance remain unqualified. See
[the r15 native alias record](build/graphics-hardware-2d-validation-20261015-r15/revision-record.json).

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
[the r16 backing-alias record](build/graphics-hardware-2d-validation-20261015-r16/revision-record.json).

The r17 extension adds Screen blending through GS complement lookups and
exact multiplication, including component-alpha masks and writable alpha
maps. It passes all 325 host cases across 15 suites, 16 targeted R5900
cases and 198 native Xorg checks under PCSX2 CPU GS emulation. The 12 new
model cases cover 1,304 requests per environment; native checks cover 27
layouts with three filters and differing depth-15 aliases. Guest software
raster fallback remains disabled. Additional formats, gradients, other
extended operators, full native regressions and physical qualification
remain pending. All-format 2D is unfinished. See
[the r17 Screen record](build/graphics-hardware-2d-validation-20261016-r17/revision-record.json).

The r18 extension adds GS-only Exclusion blending. Both integer products
remain unrounded until their sum is normalized once; alpha uses Over. It
passes 339 host cases across 15 suites, 21 targeted R5900 cases and 198
native Xorg checks under PCSX2 CPU GS emulation. Each model environment
checks 589,824 exhaustive RGB channels, 1,304 integration requests and 58
rejection guards. The rebuilt multiplication diagnostic also passes
196,608 native channel checks. Pixel arithmetic stays on GS and existing
scratch pages are reused. Additional formats, gradients, other operators,
full native regressions and physical qualification remain pending. See
[the r18 Exclusion record](build/graphics-hardware-2d-validation-20261016-r18/revision-record.json).

The r19 extension adds GS-only Multiply blending. Exact separately rounded
products and clamped sums stay in GS VRAM, including independent component-alpha
and scalar alpha factors. It passes 351 host cases across 15 suites, 20 targeted
R5900 cases and 198 native Xorg checks under PCSX2 CPU GS emulation. The
native multiplication primitive passes 196,608 channel checks. Twelve new
model cases cover three filters, packed layouts, coverage, alpha maps, shared
backing and solid masks; existing scratch pages are reused. Additional formats,
gradients, other operators, full native regressions and physical qualification
remain pending. See
[the r19 Multiply record](build/graphics-hardware-2d-validation-20261016-r19/revision-record.json).

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
[the r20 ordered-blend record](build/graphics-hardware-2d-validation-20261016-r20/revision-record.json).

The r21 extension adds GS-only Overlay and HardLight. GS selects each RGB
branch, retains signed unrounded products and performs one final normalization.
It passes 415 sanitized host cases across 15 suites, 39 targeted R5900 cases,
396 native Xorg requests and 417,792 native arithmetic channel checks under
PCSX2 CPU GS emulation. Each model environment checks 2,211,840 RGB channels,
including 114,056 overflow vectors and 419,066 separate-rounding counterexamples.
All 153 new primitive guards pass; no software raster fallback is added.
Thirty-one operator IDs, gradients, additional formats, full native regressions
and physical qualification remain pending. See
[the r21 piecewise-blend record](build/graphics-hardware-2d-validation-20261016-r21/revision-record.json).

The r22 extension adds GS-only disjoint/conjoint Clear, Src and Dst, plus
DisjointOverReverse. The latter retains wide sampling and RGBA16 solids even
for opaque inputs, preserving its rounding difference from Saturate. It passes
508 sanitized host cases across 15 suites, 104 targeted R5900 cases and 1,412
native Xorg requests across four PCSX2 CPU GS sessions. Both model environments
also pass 489 rejection guards and the explicit opaque-source rounding check.
No software raster fallback is added. Twenty-four operator IDs, gradients,
additional formats, full native regressions and physical qualification remain
pending. See
[the r22 disjoint/conjoint record](build/graphics-hardware-2d-validation-20261016-r22/revision-record.json).

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
[the r23 ratio-factor record](build/graphics-hardware-2d-validation-20261016-r23/revision-record.json).

The r24 extension adds ColorDodge and ColorBurn through GS binary32
comparisons and conditional selection, preserving zero-alpha branches and
separate product/division rounding. It passes 747 sanitized host cases across
15 suites, 45 targeted R5900 cases and 30 native Xorg checks under PCSX2 CPU
GS emulation. The new primitives also pass 49,152 comparison lanes and
122,880 selected byte lanes per model environment; 4,096 direct blend channel
checks match an independent float oracle. No software fallback is added.
Five blend operators, gradients, additional formats, broader native tests
and physical PS2 qualification remain pending. See
[the r24 ColorDodge/ColorBurn record](build/graphics-hardware-2d-validation-20261016-r24/revision-record.json).

The r25 extension adds SoftLight with an exactly rounded GS square root
and signed binary32 arithmetic. All 50 previous wide-arithmetic functions
remain unchanged. It passes 767 sanitized host cases across 15 suites,
32 targeted R5900 cases and 20 native Xorg requests under PCSX2 CPU GS
emulation. Per model environment, the new arithmetic passes 43,125 square-root
lanes, 73,728 signed-operation lanes and 2,048 direct blend channel checks.
No software raster fallback or CPU pixel arithmetic is added. Four HSL
operators, gradients, additional formats, broader native tests and physical
PS2 qualification remain pending. See
[the r25 SoftLight record](build/graphics-hardware-2d-validation-20261016-r25/revision-record.json).

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
[the r26 HSL record](build/graphics-hardware-2d-validation-20261016-r26/revision-record.json).

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
[the r27 gradient record](build/graphics-hardware-2d-validation-20261017-r27/revision-record.json).

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
See [the r28 ten-bit record](build/graphics-hardware-2d-validation-20261017-r28/revision-record.json).

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
unfinished. See [the r29 ten-bit operator record](build/graphics-hardware-2d-validation-20261017-r29/revision-record.json).

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
qualification, performance work and Mesa GLX remain unfinished. See [the r30 ten-bit convolution record](build/graphics-hardware-2d-validation-20261017-r30/revision-record.json).

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
native/physical/performance qualification and Mesa GLX remain unfinished. See [the r31 alpha-map read record](build/graphics-hardware-2d-validation-20261017-r31/revision-record.json).

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
qualification and Mesa GLX remain unfinished. See [the r32 writable-map record](build/graphics-hardware-2d-validation-20261017-r32/revision-record.json).

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
broader runtime/physical/performance qualification remain open. See [the r33 mixed-format record](build/graphics-hardware-2d-validation-20261017-r33/revision-record.json).
See
[the r7 record](build/graphics-hardware-2d-validation-20261014-r7/revision-record.json),
[the r8 bilinear record](build/graphics-hardware-2d-validation-20261014-r8/revision-record.json),
[the r9 coverage record](build/graphics-hardware-2d-validation-20261015-r9/revision-record.json) and
[wide arithmetic evidence](docs/graphics-wide-arithmetic-validation.json).
RGB444, A4, RGB332/BGR233 and both ARGB2222 orders now use GS conversion.
RGB565, ARGB1555 and ARGB4444 in both orders also use GS conversion;
depth-16 core drawing and transfers preserve the full raw word on GS.
No other GPU driver or software raster fallback is included. See
[current development evidence](docs/graphics-hardware-2d-validation.json).

- Imported the entire PS2 source delta from pinned `frno7/linux` `ps2-main`
  against Linux 5.4.221: 208 source files, CPU/platform changes, IOP/SIF, USB,
  graphics, RTC, serial, and storage support, with original licenses, hashes,
  full patch and compatibility patch. Experimental ATA/gamepad sources and
  frno7 IOP firmware sources are imported separately with provenance.

- A real source transplant into Linux 7.2.8, with rejected hunks recorded and
  reviewed API/cache adaptations. The selected kernel, storage/input modules and compressed
  audit ELF cross-build successfully; outstanding architecture and bootstrap
  review keeps the boot-image gate closed. See
  [kernel/port-report.json](kernel/port-report.json) and [hardware support](docs/hardware.md).

- A genuine GNU/Linux o32 soft-float R5900 compiler bootstrap: GNU binutils,
  GCC C/C++, and glibc. ps2dev's IOP compiler builds the coprocessor firmware.

  SDK/newlib programs and Linux/glibc programs have different ABIs.

  The latest build uses GCC 16.2.0, binutils 2.47 and glibc 2.44; all 38 base
  target packages compiled and eight R5900 GNU smoke tests passed.

- Pinned Buildroot dependency-resolving recipes for GNU Bash, coreutils,
  findutils, diffutils, grep, sed, gawk, tar, compression, editors, networking,
  disk administration and optional GNU development tools. Core commands use
  GNU implementations; dhcpcd supplies DHCP. BusyBox is disabled and rejected
  by the runtime image audit.

- The base profile's 38 target packages actually cross-build. A 64 MiB ext4
  image contains GNU tools and SysVinit, with checked ELF/library closure and
  IOP firmware. The full native GNU development profile and current X.Org apps
  also cross-build. The normal `startx` entry point is checked separately from
  package compilation and physical console qualification.

- Real SysVinit source in `userland/sysvinit` supplies the system's `/sbin/init`.
  The `boot/early-root.c` helper performs disk-root handoff and RAM reclamation;
  boot USB/storage/input drivers are built into the kernel.

  authenticated console login, bounded temporary filesystems, DHCP/static IPv4,
  renewal/deconfiguration, optional existing swap, and reproducible ext4/tar
  packaging without writing any physical disk.

- English UK console keyboard loading before login, with `gb` selected for Xorg.
  The console map is supplied by the pinned upstream kbd tools.

- A native SMAP PIO/NAPI/phylib Ethernet driver, built into the kernel with
  its ordered SIF/IOP bridge. Matching DEV9/IRQ firmware covers Slim, expansion
  bay and PC Card paths. Other Ethernet drivers and boot modprobe calls are
  disabled. Basic physical Ethernet is confirmed on the SCPH-90001; other
  console and adapter variants still require acceptance tests.

- Cross-built Xorg 21.1.24, TWM 1.0.12, xterm, fonts and desktop helpers,
  with a PS2 theme and classic `startx` entry point. Hardware-only startup
  explicitly stops while the native PS2 Xorg drawing driver is absent.

- Separate generic PS2 framebuffer and GS command patches. The command engine issues
  native GS fills, local copies and depth-tested Gouraud triangles, validates whole
  batches and waits for GS completion. A CPU mapping is available for upload
  diagnostics; it is not an accelerated renderer. The corrected triangle scene and
  console restoration are user-confirmed on SCPH-90001. Shared GS transport, Xorg EXA
  callbacks and a Mesa draw/raster stage now cross-build and pass callback tests.
  Bounded native image transfers connect Xorg `UploadToScreen` and Mesa opaque
  front-buffer uploads to the GS. Exclusive GS sessions and offscreen allocations now
  connect native Xorg EXA pixmap callbacks and Mesa resource objects. Allocated depth
  and offscreen triangles now connect the same owner to real Mesa resources, with
  Xorg/Mesa drawing order enforced. Native RGB texture sampling, perspective
  coordinates, cache invalidation, Mesa sampler views and an EXA texture-copy component
  now cross-build and pass eleven new host/R5900 checks. The user confirms the PCSX2
  panels and text return. Actual Gallium screen/context callbacks now retain resources
  and sampler views through deferred destruction; Xorg holds a context lease until its
  callbacks are restored. Shader caps remain zero and `draw_vbo` rejects unimplemented
  shader execution. Native core bitmap glyph callbacks now use GS fills, composite
  clipping and a Mesa context lease. Its PCSX2 probe exits zero and the supplied
  screenshot matches the intended three-row pattern. A GC lifecycle layer now manages
  creation, validation, copying, clips and destruction, and routes ordinary 8/16-bit
  text through the native renderer. The eighth startup probe exits zero in PCSX2; the
  supplied screenshot matches the intended text and central clip gap. Native solid
  drawing now supports all sixteen Boolean ALUs and RGB plane masks, including
  reversible XOR. Its PCSX2 probe exits zero; the user confirms the grid, reversible
  outline and console return. Six native Xorg solid GC callbacks now handle clipped
  fills, points and thin axis-aligned lines/outlines through a shared Mesa batch.
  Thirteen new host/R5900 cases and eleven GC/text regressions pass. Its PCSX2 probe
  exits zero and the user confirms the panel, outline erasure and console return. On
  2026-10-04 the user confirms the ten existing graphics runtime diagnostics work on the
  real PS2, recorded as user-confirmed verification. The normal primitive, upload,
  surface, depth, texture/context, glyph/GC, Boolean drawing and solid panel scenes now
  have [physical hardware evidence](docs/graphics-hardware-validation.json). Native Xorg
  `CopyArea` and shared Mesa region copies now handle clipping, overlap and odd-width
  tails with GS drawing. Twelve new host sanitizer checks and thirty R5900 checks pass.
  This new eleventh scene still needs real hardware verification. Mesa's actual Gallium
  resource copy callback now uses the native GS adapter; seven new checks plus 39 shared
  Xorg/context regressions pass on both the host and R5900. Its [generic patch and
  callback evidence](docs/graphics-resource-copy-validation.json) are separate from the
  preserved diagnostic image's qualification. EXA pixmap copy callbacks now use the same
  Mesa GS batch, including odd widths and overlapping moves; eight new adapter checks
  and sixteen shared copy regressions pass on both host and R5900. The [generic EXA
  patch and evidence](docs/graphics-exa-copy-validation.json) record this component's
  implementation and pending runtime pixel checks. Mesa's Gallium `blit` now submits
  native GS nearest scaling, source flips and scissored transfers. Xorg's opaque EXA
  `PictOpSrc` copies share the same adapter. Eleven new cases and thirty regressions
  pass on both host and R5900; [generic patches and blit
  evidence](docs/graphics-blit-validation.json) record the supported states. Its
  [runtime evidence](docs/graphics-blit-runtime-validation.json) also records four
  lifecycle checks per environment; new physical/RGB555 runtime qualification remains
  pending. Xorg's native screen `CopyWindow` now moves window contents through the
  shared Mesa GS copy batch, including clipped overlap and redirected pixmap origins.
  Eleven new cases and 41 regressions pass per environment; see [CopyWindow component
  evidence](docs/graphics-copy-window-validation.json). Its [text-only runtime
  evidence](docs/graphics-copy-window-runtime-validation.json) records user-confirmed
  scene panels and console return; physical/RGB555 pixel qualification remains pending.
  Native `PaintWindow` paints solid and tiled backgrounds/borders through the shared
  Mesa GS pipeline, with inherited alignment and redirected RGBX/ RGB555 storage. Small
  tiles expand into a bounded native GS block. Thirteen new cases and 38 regressions
  pass on host and R5900; see [tiled-painting
  evidence](docs/graphics-tiled-paint-validation.json). PCSX2 completes all four scene
  command checks with exit zero and text-mode restoration; [text-only runtime
  evidence](docs/graphics-tiled-paint-runtime-validation.json) records matching supplied
  panels for scenes 2–4 and user confirmation of all four scenes and console return.
  Physical qualification remains pending. Native GC `FillSpans` and `PolyFillRect` now
  repeat owned RGBX/RGB555 tiles through the same Mesa GS adapter, with GC pattern
  origins, composite clipping and redirected backing offsets. Pixel tiles use the native
  Boolean path. Fifteen new checks and 59 regressions pass on host and R5900; see [tiled
  GC evidence](docs/graphics-tiled-gc-validation.json). PCSX2 completes all four
  command/fence checks with exit zero and text-mode restoration; see [text-only fill
  runtime evidence](docs/graphics-tiled-gc-runtime-validation.json). All four supplied
  PCSX2 panels match the expected scenes, and the user confirms console return. New
  physical qualification and actual server integration remain pending. Opaque stippled
  GC rectangles/spans now construct foreground/background patterns on GS from existing
  monochrome source bitmaps and repeat them through Mesa's native tile adapter. Fourteen
  new checks and 74 regressions pass per environment; see [opaque stipple
  evidence](docs/graphics-stippled-gc-validation.json). The separate
  `ps2-gs-stipple-test` adds five lifecycle checks per environment and preserves all
  fifteen preceding diagnostic binaries. PCSX2 completes its four command/fence scenes
  with exit zero and text-mode restoration; see [text-only stipple runtime
  evidence](docs/graphics-stippled-gc-runtime-validation.json). Four supplied PCSX2
  panels match the expected scenes, and the user confirms console return. Physical
  qualification and actual Xorg/fb bitmap client execution remain separate. Source-only
  monochrome pixmaps now have driver-owned, zeroed storage with server scanline padding,
  bounded allocation and EXA access/cleanup hooks. Native stipple fills read that
  storage while EXA hides its CPU pointer; depth-1 GCs retain lower bitmap-editing
  callbacks. Ten new cases and 88 drawing regressions pass on host sanitizers and R5900.
  See [monochrome pixmap evidence](docs/graphics-monochrome-validation.json). The
  separate `ps2-gs-mono-test` passes five lifecycle checks per environment, preserves
  sixteen preceding diagnostic binaries, and completes all four PCSX2 command/fence
  scenes with exit zero and text-mode restoration. The user confirms all four scenes and
  console return; supplied panels match. See [text-only runtime
  evidence](docs/graphics-monochrome-runtime-validation.json). Actual Xorg client/fb
  execution, physical pixels and the full DDX/GLX/TWM session remain pending.
  Transparent stippled GC rectangles/spans now preserve destination pixels at zero bits
  and support all 16 Boolean ALUs and partial RGB masks through GS. Sixteen new plus 98
  regression checks pass on host sanitizers and R5900; see [transparent fill
  evidence](docs/graphics-transparent-validation.json). The distinct
  `ps2-gs-transparent-test` adds six lifecycle checks per environment and preserves all
  seventeen earlier diagnostics. PCSX2 completes all four command/fence scenes with exit
  zero and text-mode restoration; see [text-only runtime
  evidence](docs/graphics-transparent-runtime-validation.json). All four supplied panels
  match, and the user confirms console return. Physical pixels and full server execution
  remain separate. Opaque stipples now also support all 16 Boolean ALUs and partial RGB
  masks. Disjoint foreground/background runs apply each pixel operation once; optimized
  GXcopy/full-mask repeat remains. Sixteen new plus 114 regressions pass per
  environment; see [opaque Boolean
  evidence](docs/graphics-opaque-stipple-logic-validation.json). The distinct
  `ps2-gs-opaque-test` adds six lifecycle checks per environment and preserves eighteen
  preceding diagnostics. PCSX2 completes all four scenes with exit zero and text-mode
  restoration; see [text-only runtime
  evidence](docs/graphics-opaque-stipple-logic-runtime-validation.json). All four opaque
  Boolean panels match the supplied images; console return awaits user confirmation.
  Physical pixels and full server execution remain separate. Zero-width solid lines now
  support all eight octants, Xorg screen biases, clipping and XOR joins through shared
  Mesa geometry and native GS Boolean runs. Twelve new plus 130 regressions pass on host
  and R5900; see [thin-line evidence](docs/graphics-thin-lines-validation.json). The new
  `ps2-gs-line-test` adds six lifecycle checks per environment and preserves nineteen
  earlier diagnostics. PCSX2 completes all four scenes with exit zero and text-mode
  restoration; see [text-only runtime
  evidence](docs/graphics-thin-lines-runtime-validation.json). All four line panels
  match supplied images, and the user confirms console return. Physical pixels and full
  desktop qualification remain separate. Thin on/off and double-dashed lines now
  preserve phase through clipping, joins and rectangle corners, using native GS coloured
  Boolean runs. Sixteen new plus 142 regressions pass per host/R5900 environment; see
  [dashed-line evidence](docs/graphics-thin-dashes-validation.json). The separate
  `ps2-gs-dash-test` passes six lifecycle checks per environment, preserves twenty
  earlier diagnostics and completes all four PCSX2 scenes with exit zero/text-mode
  restoration. The user confirms all four scenes and console return; see [text-only
  runtime evidence](docs/graphics-thin-dashes-runtime-validation.json). A native
  monochrome pointer cursor now saves/restores its background in GS storage, hides
  before shared front reads/writes and connects Xorg pointer, Damage/block and cleanup
  callbacks. Sixteen new plus 158 regressions pass per host/R5900 environment; see
  [cursor evidence](docs/graphics-native-cursor-validation.json). `ps2-gs-cursor-test`
  adds six lifecycle checks per environment and distinct cursor
  movement/copy/restoration scenes, with 21 prior binaries preserved. All four new PCSX2
  panels match visually, and the probe exits zero after restoring text mode; see [cursor
  runtime evidence](docs/graphics-native-cursor-runtime-validation.json). USB pointer
  input and the complete X server lifecycle remain unqualified. Native colour
  PutImage/SetSpans now handle clipped client images through Mesa GS uploads and Boolean
  runs. Sixteen new plus 174 regressions pass per host/R5900 environment; see
  [client-image evidence](docs/graphics-client-images-validation.json). The new
  diagnostic completes all four scene commands and restores text mode in a 32 MiB PCSX2
  guest; [runtime evidence](docs/graphics-client-images-runtime-validation.json) records
  the exact run. All four supplied panels match, and the user confirms normal console
  return. The distinct ps2-gs-image-test adds six lifecycle checks per environment and
  preserves 22 preceding diagnostic binaries. Native XYBitmap/XYPixmap PutImage now
  handles foreground/background bitmaps and descending colour planes through Mesa GS
  colour fills and Boolean runs, including clipping, left padding and RGB masks. Sixteen
  new plus 190 regressions pass per host/R5900; see [planar-image
  evidence](docs/graphics-planar-images-validation.json). The distinct
  ps2-gs-planar-test adds six lifecycle checks per environment, with 23 prior diagnostic
  binaries preserved and 57 earlier probe checks passing. The corrected full-copy path
  passes all four PCSX2 scenes and console return, including matching final backing
  zigzags; the failed first run is preserved. Native solid FillPolygon now adds convex,
  concave and self-intersecting shapes, both fill rules and coordinate modes through
  bounded Mesa geometry and GS drawing. Eighteen new plus 206 regression checks pass per
  host/R5900; see [polygon evidence](docs/graphics-filled-polygons-validation.json). The
  polygon probe exits zero in 32 MiB PCSX2; all four supplied scenes and user-confirmed
  console return pass qualitative review. Six lifecycle checks pass per environment,
  plus 63 earlier host probe checks; see [polygon runtime
  evidence](docs/graphics-filled-polygons-runtime-validation.json). Transparent and
  opaque stippled polygons now use the same geometry with snapshotted monochrome sources
  and native GS runs. All 242 component checks and six diagnostic checks pass per
  host/R5900 environment, plus 69 earlier host probe checks; see [stippled polygon
  evidence](docs/graphics-stippled-polygons-validation.json). All four stippled-polygon
  PCSX2 scenes pass qualitative review, with exit zero and user-confirmed console
  return; see [runtime
  evidence](docs/graphics-stippled-polygons-runtime-validation.json). The first
  transformed 3D component now adds explicit MVP projection, homogeneous clipping and
  native GS triangles/Z16, presented through actual Xorg EXA Copy callbacks. All 261
  component checks and six diagnostic checks pass per host/R5900 environment; see [3D
  component evidence](docs/graphics-transformed-triangles-validation.json). All four
  still 3D scenes pass qualitative review with user-confirmed console return and probe
  exit zero in 32 MiB PCSX2; see [3D runtime
  evidence](docs/graphics-transformed-triangles-runtime-validation.json). This scalar
  geometry API still leaves shader execution, `draw_vbo`, VU programs and GLX
  unfinished, with shader caps held at zero. Full TWM/OpenGL sessions and desktop RAM
  measurements remain separate checks. Alpha glyphs, further formats/sampler states,
  full DDX screen setup and Mesa/GLX integration remain outstanding; these components
  are not installed as a working desktop. See [graphics implementation and
  evidence](docs/graphics.md).

- A small `ps2-video` selector, checked GS memory allocation and RGB output
  for VGA timings, EDID-based selection when available, and timed previews up
  to 1920×1080. Explicit EDID auto selection preserves the boot mode without
  display information. The tested display chain accepts 576p but reports
  blank output for 1080i and 1080p.

  See [console resolutions](docs/video-modes.md).

- A pinned R5900 QEMU Linux-user build and eight passing real GNU smoke tests,
  plus a disposable PCSX2 USB disk/preset and boot-stage checklist.

It contains matching FAT32 boot files and ext4 GNU root, with `root` / `eelinux`.

The user confirms relay-v8 boots to an authenticated root shell on the real
SCPH-90001. It retains the corrected page-cache and interrupt paths, USB/ext4
root and GNU dynamic userland. The earlier SMAP probe timeout is recorded in
[the previous observations](docs/hardware-network-v8.json). With the current
IOP backend, the user now confirms physical Ethernet on the SCPH-90001:
`eth0` has dynamic IPv4 address `192.168.2.112/24`, and an external ping
received all four replies with zero packet loss. The user also confirms
successful `ping gnu.org` and no messages from `dmesg -l err`. See
[physical Ethernet evidence](docs/hardware-network-validation.json).
The UK backslash/pipe keys still need physical-keyboard diagnosis.

The current `ps2-smap` candidate moves FIFO, MAC and PHY access to the IOP,
with bounded packet RPCs to Linux. The real-console error was identified as
a TX FIFO reset timeout on the legacy EE register path. This candidate is
intended for Slim Ethernet and Fat consoles with an official Ethernet adaptor.
Basic physical connectivity and DNS are user-confirmed on the SCPH-90001;
other models and sustained traffic still need testing. All 239 checks
pass, and PCSX2 DHCP, DNS and external ping pass with zero RX/TX errors.
All exported patches now use generic PS2 names. See
[patches and application order](kernel/patches/README.md) and
[current driver evidence](docs/ps2-smap-validation.json).

See [hardware testing](docs/hardware-test.md),
[startup evidence](docs/native-smap-startup-validation.json) and
[userland investigation](docs/hardware-userland-validation.json).

## Start

This checkout contains source, package recipes, source locks, patches and
validation summaries. Downloaded upstream archives, compiler output, local
emulator installations and console BIOS/ROM files are kept outside public Git
history. Builds fetch missing locked archives into their work directories;
`userland/sources` is an optional legacy cache, not a required source import.
The vendored PS2 source delta and complete SysVinit source remain included and
are checked by `make check`.

Website files and release media are published separately from this source
repository. The source browser is intended for
<https://eelinux.falanet.org/git/>. See [cgit hosting](docs/cgit-hosting.md) for
the account-local server installation and SSH publishing workflow.

Use Linux or the installed Debian WSL2 environment. Keep downloaded kernel
sources and build output on a **native Linux filesystem**: Windows cannot
represent all Linux kernel filenames and filesystem permissions.

```powershell
.\build.ps1 check
.\build.ps1 plan
.\build.ps1 kernel-port
```

On Linux:

```sh
make check
make plan PROFILE=desktop
make kernel-port
make kernel-audit
```

`kernel-port` and `kernel-audit` deliberately return a failure while the recorded

7.2.8 compatibility work remains. They do not create a falsely labelled bootable
image. `make all` builds the modern system only after that gate passes.

For an explicit older reference build, see `make reference-system` in
[docs/build.md](docs/build.md). It does not satisfy the 7.2.8 target and still
requires hardware validation. Build profiles are `base`, `desktop` (default),
and `full`; the full GNU development suite needs more disk space and swap.

Disk footprint and runtime RAM are different budgets: the whole GNU system
must not be placed in initramfs.
The minimal Xorg/Mesa/TWM desktop targets 32 MiB main RAM, but total and peak
desktop usage remain unmeasured. The reported roughly 7 MiB console usage
will increase when Xorg, TWM and applications run. See the
[memory budget and measurement plan](docs/graphics.md#main-ram-budget).

## Emulator test step

Follow [docs/emulator.md](docs/emulator.md) for the runnable GNU smoke test and
PCSX2 preparation/launch commands. `make emulator-userland-build` builds the
pinned R5900 fork; `make emulator-smoke` tests an existing base GNU staging tree.

All eight actual smoke checks passed. The guide includes the exact command for
the completed WSL builds in this workspace.

The project-patched Linux and Windows PCSX2 builds mount a virtual USB root,
start SysVinit and pass the target PAM checks for `root` / `eelinux`. The Windows
launcher verifies and selects the project runtime automatically. Stock 2.6.3
still stalls on userspace memory faults. See [authentication evidence](docs/authentication-validation.json)
and [the launch guide](docs/emulator.md).

It uses FAT32 for LaunchELF and ext4 for `/dev/sda2`, GNU tools without BusyBox,
dhcpcd, UK keys and native SMAP. Login is `root` with password `eelinux`.

USB/storage/input and IOP register drivers are built in, with an empty early
kernel-module manifest. The separate IOP processor still requires IRX firmware.

The R5900 CP0 correction cleared the physical blank-screen failure: the user
confirmed Linux text on the SCPH-90001. The relay-v2 retest produced a kernel
Oops in a memory-copy load; the fault registers scrolled off the screen.

Relay-v3 corrects receive-page cache ownership before IOP DMA writes and retains
named EPC/RA, fault registers, copy arguments and the last initcall below the
stack trace. A regression test reproduces the old stale-DMA-reply defect. This
defect is not yet confirmed as the cause of the physical panic. Physical
USB-root mounting is now user-confirmed; physical root login is user-confirmed. The earlier relay-v3 hardware retest stopped
while LOADFILE started the IRQ relay; it repeated `SIF: waiting for RPC 6` and
also reported a SMAP control timeout. Relay-v4 replaces cached EE writes into
IOP heap memory with aligned SIF DMA transfers. Relay entry uses a larger RPC
thread stack and a shared startup mailbox instead of synchronous debug output.

The rebuilt IOP firmware also has aligned file/BSS boundaries and
module sizes that include read-only data and alignment gaps. An unanswered RPC prints one warning, then only changed startup phases; its
DMA/client memory remains owned. PCSX2 USB/ext4 boot, UK keys, DHCP, DNS and
external ping pass on this exact kernel. Physical retesting is still required.

`scripts/emulator.py refresh` retains the previous session and updates its kernel/disk
at the same path. The launcher prints the selected kernel hash and rejects a stale copy
after that source is rebuilt.

The installed stock Windows emulator still has its original MMU limitations.

For the SCPH-90001/Tuna USB test, follow [hardware-test.md](docs/hardware-test.md).

The single-stick image uses FAT32 for the launch ELF and ext4 for `/dev/sda2`.

PAL remains the default. Earlier color and cache-handoff probes are preserved
as historical evidence.

## Remaining work

The complete roadmap and acceptance criteria are in root [TODO.md](TODO.md).

Highest priorities are the 7.2.8 R5900 architecture port, physical USB and
DEV9/SMAP qualification, and physical qualification of the implemented GS buffer/upload
backend for Xorg. [Ethernet evidence](docs/ethernet.md) explains the imported
GPL sources and hardware register references; [runtime instructions](docs/runtime.md)
cover networking, accounts, memory, and desktop configuration.

Host tests and cross-compilation do not prove console boot, PHY correctness,
DMA coherence, or operation within 32 MiB. The user's SCPH-90001 now displays
Linux boot output and mounts ext4 USB root; physical root login is confirmed; Ethernet remains unqualified.

[Validation evidence](docs/validation.md) records
the checks and their limits. License: GPL-2.0-only for new project code;
imported source and package licenses remain as supplied by their authors.
