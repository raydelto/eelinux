# eeLinux implementation roadmap

ee = Emotion Engine. Target: Linux 7.2.8, working PS2 drivers and Ethernet,
GNU userland and Xorg/TWM within 32 MiB. Updated 2026-10-18.

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

## Earlier graphics development

Remaining-work statements in the following history refer to those revisions.

The focused native Xorg alpha-map run passes 981 pixel checks with PCSX2 CPU
GS emulation. The r5 module passes 24 native window and 114 packed-map
alias checks; a GS integer division diagnostic passes 24,576 channel comparisons.
At r5, 239 distinct model cases passed in each development
environment. Shifted packed aliases except A8 and differing converted-format
aliases stopped explicitly at that revision. The r12 extension below adds
shifted same-format aliases; r14 adds byte/word-format aliases below.
The wider GS arithmetic component passes native 24-bit integer products and
binary32 multiply, divide, add and subtract probes. GS nearest UNORM8 Saturate
passes 8,192 native RGBA pixels with scalar/component-alpha masks. Its new Xorg
dispatch passes host and R5900 models. The r7 packed extension passes 56 native
Xorg checks across four byte and ten word layouts, with component-alpha masks.
Its kernel supports 128 bounded handles while retaining the 4 MiB VRAM limits.
The r6 native matrix failed an opaque-source rounding check; r7 corrects the
GS operator reduction, and the 126-check matrix passes. The development
bilinear extension retains all 16 fraction bits and passes 12 focused native
Xorg checks. Its full host regression passes 267 cases across 15 suites; the
r8 console package is hash-verified. The r9 coverage extension adds A1/A4/A8
bilinear and component-alpha Saturate, passing 24 native Xorg checks and four
targeted R5900 cases. Alpha-only results use GS multiply/add/clamp, avoiding
division and unused RGB factors. Its full host regression passes 274 cases
across 15 suites. Wide convolution and the remaining
formats/operators are pending. See [the r9 record](build/graphics-hardware-2d-validation-20261015-r9/revision-record.json).
The r10 solid extension preserves original RGBA16 colours for solid-source and
scalar/component-alpha solid-mask Saturate. Its 24 focused native Xorg checks
pass; the four focused host and R5900 cases include 960 scenarios and eleven
guards per environment. All new test code is in the existing `.c` files.
The full host regression passes 278 cases across 15 suites, with the four new
cases rerun on host and R5900 after merging the helpers into `.c`.
See [the r10 record](build/graphics-hardware-2d-validation-20261015-r10/revision-record.json).
The r11 alpha-map Saturate extension passes 48 focused native Xorg checks.
Its host/R5900 models cover 592 mapped scenarios; parent and map stores retain
independent precision on GS. Nine focused R5900 cases pass, including 26 new
primitive guards. At r11, wide convolution remains disabled: its pinned float-to-u32
conversion differs between the host and the R5900 soft-float runtime.
The r11 full host regression passes 287 cases across 15 suites. See
[the r11 record](build/graphics-hardware-2d-validation-20261015-r11/revision-record.json).

The r12 shifted-map extension passes 12 focused native Xorg checks under
PCSX2 CPU GS emulation. GS scanline staging preserves parent/map store order,
and packed writes quantize before later scanlines read them. Host and R5900
models each pass 1,610 new alias scenarios. The full host regression passes
292 cases across 15 suites; seven targeted R5900 cases pass. See
[the r12 record](build/graphics-hardware-2d-validation-20261015-r12/revision-record.json).

The r13 convolution extension reproduces the R5900 per-tap unsigned casts
and final signed clamp on GS. All 12 focused native Xorg checks pass,
including extreme-coefficient overflow/cancellation and writable-map aliases.
The full host regression passes 299 cases across 15 suites; 13 targeted
R5900 cases pass, including the six expanded legacy coverage cases. All
pixel processing stays on GS. See
[the r13 record](build/graphics-hardware-2d-validation-20261015-r13/revision-record.json).

The r14 extension synchronizes different byte/word-format writable maps
through GS raw backing, preserving scanline store order and observable padding.
Its 32 focused native Xorg checks pass under PCSX2 CPU GS emulation. Host
and R5900 models each pass 1,927 shared-backing comparisons across 120
ordered format pairs, plus nine rejection guards. The full host regression
passes 303 cases across 15 suites; seven targeted R5900 cases pass. Native
15-bit converted aliases, remaining formats/operators, full native matrices
and physical memory/performance qualification remain open. See
[the r14 record](build/graphics-hardware-2d-validation-20261015-r14/revision-record.json).

The r15 extension adds native depth-15 RGB555/RGB444 writable format
aliases. GS converts native CT16 storage to packed raw words, synchronizes
both decoded views in scanline store order, and transfers the final raw
result back to CT16. All 24 focused native Xorg checks pass under PCSX2
CPU GS emulation. Host and R5900 models each check every 15-bit raw value
in four layouts, 196 alias comparisons and 25 rejection guards. The full
host regression passes 308 cases across 15 suites; ten targeted R5900
cases pass. Broader native matrices, additional formats, gradients,
extended operators and physical memory/performance remain unqualified. See
[the r15 record](build/graphics-hardware-2d-validation-20261015-r15/revision-record.json).

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

**Current state:** GCC 16.2.0, binutils 2.47 and glibc 2.44 are built; all
38 base packages and eight GNU Linux-user smoke tests passed. Native SMAP,
USB/storage/input and the PS2 bridge are built in; six IOP firmware modules
remain necessary for the separate coprocessor. GNU tools replace BusyBox;
dhcpcd supplies DHCP, UK keys are selected, and root is ext4.

Current X.Org applications now build from the existing x11r7 recipes: 61
application/data releases plus runtime dependencies. TWM uses XDG desktop
entries and starts a classic analogue xclock. Production `pal-media` targets create an
exact 2 GB USB image and separate Slim/Fat firmware-specific FreeDVDBoot DVDs,
without project test clients/services. See [the media guide](docs/pal-desktop-media.md)
for the Fat PAL compatibility limits and physical qualification still needed.
The full GNU development profile and SDK are preserved in USB and standalone
DVD payloads. DVD roots use native optical reads, an 8 MiB RAM overlay and
bounded LZ4 compressed RAM swap; eight-sector transfers reduce disc reads.

The native Xorg DDX now runs TWM and xterm at depth 15 in 32 MiB PCSX2.
The extended session passes four drawing scenes, CopyPlane, pointer movement,
window resizing, terminal scrolling and interactive typing/backspace, then
restores the console with client/server/session exit zero. The user confirms
the successful terminal result. Full 2D coverage and the latest image's physical
qualification remain open; Mesa shader/GLX execution is still unfinished.
The clean Linux 7.2.8 build includes 850 C objects, 22 assembly objects and
11 modules with W=1 and warnings treated as errors, with zero build warnings.

The GS-only 2D extension implements all sixteen colour-source copy and tile
ALUs with native RGB masks, tiled polygons, colour GetImage/GetSpans, and
installs opaque XRender source copies in the production DDX. The 273 previous
component cases and sixteen new Boolean/tile/readback cases pass on host
sanitizers and R5900, including exhaustive byte-pair Boolean checks. Native
kernel transfer/readback checks pass and the updated kernel/DDX cross-build.
Revision r10 passes 141 native pixel comparisons and the complete TWM/xterm
session with zero client errors and Xorg exit zero. The user confirms the
stripe and black-square fixes; see
[runtime evidence](docs/graphics-gpu2d-runtime-validation.json). Full coverage,
cursor presentation during readbacks and physical qualification remain open.
The 2026-10-13 source adds colour-source CopyPlane, exact A8 masks, translucent
solid sources, basic Porter–Duff operations on opaque RGB, nearest projective
sampling, bilinear coverage and direct/accumulated A8 glyphs. Core text supports
all ALUs, RGB plane masks and tiled fills. The production kernel/module pass
726 native Xorg comparisons at depth 15 in 32 MiB PCSX2, with zero client errors
and server exit zero; the native lookup test passes 90,112 pixel comparisons.
The subsequent filter extension adds exact bilinear and signed convolution
for native RGBX/RGB555 colour and A1/A8 coverage, including fast/good/best aliases.
It passes 834 native Xorg comparisons (500 Render comparisons against pixman),
168 host sanitizer cases and 168 R5900 Linux-user cases. See
[the graphics guide](docs/graphics.md) and
[filter evidence](docs/graphics-filters-validation.json). The subsequent format
extension adds both red/blue orders of RGBX/RGB555 and all sixteen conversions,
including filters, solids and aliases. It passes 998 native Xorg comparisons
(664 Render comparisons), 294,912 native RGBA mapping comparisons, and 177
component cases per host/R5900 environment; see
[format evidence](docs/graphics-formats-validation.json). The RGBA extension adds
raw depth-32 core pixmaps, all Boolean ALUs and alpha masks, and varying ARGB
alpha in both byte orders with thirteen basic operators and nearest/bilinear/
convolution. It passes 1,442 native Xorg checks (948 Render comparisons),
1,105,920 rectangle-map comparisons, 491,520 raw32 solid comparisons and 187
component cases in each host/R5900 environment. Qualification uses PCSX2's GS
software host renderer; OpenGL fails a raw32 alpha-byte comparison with the
same kernel/probe, with cause unresolved. See
[RGBA evidence](docs/graphics-rgba-validation.json). The subsequent coverage
extension adds A1/A8 sources to all six RGB/ARGB layouts through GS, and colour
alpha to A1/A8 destinations. It passes 2,414 native Xorg comparisons (1,920
Render comparisons), with 193 component cases per host/R5900 environment;
see [coverage evidence](docs/graphics-coverage-validation.json). The completion target
includes all XRender formats and filters: component alpha, additional colour
layouts, gradients, alpha maps, ARGB Saturate and extended operations remain
open. Existing shipping images predate this extension.
No CPU colour framebuffer renderer or software Mesa fallback is added.

The real SCPH-90001 boots to an authenticated GNU root shell with USB/ext4
root. The user confirms native SMAP DHCP, external ping and DNS, and no
`dmesg -l err` output. Physical UK backslash/pipe input still needs confirmation.
Other hardware models and sustained Ethernet traffic remain unqualified.

The kernel retains the corrected page writeback, folio offsets and interrupt
guards. Xorg 21.1.24, TWM 1.0.12 and desktop dependencies cross-build. Native
GS commands and native Xorg/Mesa components are implemented; the ten earlier
graphics runtime diagnostics are user-confirmed on real PS2 hardware as of
2026-10-04, recorded as user-confirmed verification. Complete accelerated Xorg and Mesa drivers
remain unfinished. Native CopyArea and bounded Mesa region copies now pass
12 new host and 30 R5900 checks; their new real hardware scene is pending.
The actual Gallium resource copy callback now uses this GS adapter and passes
46 host and 46 R5900 checks, including 39 regressions. Hardware-only `startx`
rejects a missing native DDX. EXA pixmap copies now share the Mesa GS batch;
eight new adapter cases and sixteen copy regressions pass on host and R5900.
Gallium blits now add native nearest scaling, source flips and scissoring,
with a shared opaque EXA composite adapter; eleven new cases and thirty
regressions pass in each environment. The new RGBX blit/composite diagnostic
passes in PCSX2; supplied screenshots match all scene panels. Physical/RGB555
runtime qualification remains pending.
A native screen `CopyWindow` hook now routes window moves through the shared
Mesa GS copy batch. Eleven new sanitizer/R5900 cases plus 41 shared regressions
pass in each environment; redirected RGBX/RGB555 pixmap origins and overlap
ordering are covered. The new window diagnostic adds four lifecycle checks per
environment and passes all four PCSX2 scene command checks with exit zero and
console restoration. The user confirms all four panels and console return in
PCSX2; physical/RGB555 pixels and full X server integration remain open.
Native screen `PaintWindow` now handles solid and tiled backgrounds/borders,
inherited tile alignment, redirected storage and precise Damage coordinates.
Small patterns expand on GS into a reusable block of at most 64x32 logical
pixels. Thirteen new sanitizer/R5900 cases plus 38 related regressions pass
per environment. A separate paint diagnostic adds five lifecycle checks per
environment and completes all four PCSX2 scene command checks with exit zero
and text-mode restoration. Supplied PCSX2 panels match scenes 2–4; the user
confirms all four scenes and console return. Physical and actual server qualification remain pending.
Native GC `FillSpans` and `PolyFillRect` now use the shared Mesa repeat adapter
for owned tiles, including non-power-of-two periods, GC pattern origins,
composite clipping and redirected RGBX/RGB555 storage. Pixel tiles use native
Boolean drawing. Fifteen new checks plus 59 regressions pass per environment;
the separate fill diagnostic adds five lifecycle checks and four visible scenes.
PCSX2 completes their command/fence checks with exit zero and text-mode restoration.
All four supplied fill panels match the expected scenes; the user confirms all
four and console return. New physical qualification and actual server integration
remain separate. Opaque stippled rectangle/span fills now build their colour
pattern on GS from an existing server monochrome source bitmap, with fourteen
new and 74 regression checks plus five probe lifecycle cases per environment.
PCSX2 completes all four new command/fence scenes with exit zero and text-mode restoration.
Four supplied stipple panels match the expected scenes; the user confirms all
four scenes and console return. New physical qualification and actual Xorg/fb source editing remain
separate. Source-only monochrome allocation, EXA access and GC callback routing
now pass ten new and 88 regression cases per environment; the separate owned
bitmap probe passes five lifecycle cases per environment. PCSX2 completes all
four scenes with exit zero; the supplied panels and user confirmation qualify
all four scenes and console return. The source acceptance gate remains closed with 64 rejected port files.

## Video compatibility

- [x] Provide one checked mode catalog for boot profiles and the runtime selector,
  including 800×600, 1024×768, 1280×1024, 720p and 1920×1080.

- [x] Bound virtual geometry and check proposed padded GS allocation with
  palette/font space; choose 16 bpp for modes that cannot fit at 32 bpp.

- [x] Select RGB for VGA timings and component for TV/HDTV timings.

- [x] Use valid EDID only; missing/invalid EDID preserves PAL/current boot mode.
  Restrict automatic selection by cable family; exclude experimental 1080p.

- [x] Restore previews on timeout and termination signals; verify parsing,
  selection and actual allocation checks under ASan/UBSan.

- [x] Confirm physical 576p/50 preview removes terminal flicker on the user's
  SCPH-90001 and HDMI setup. Make 576p the kernel, Make, packaging and startup
  default, retaining an explicit PAL compatibility setting.

- [x] Record physical 1080i/1080p blank output on this display chain; retain
  576p rather than selecting these unqualified modes.

- [ ] Qualify each timing on real displays and converters; record exact cable,
  PS2 model, accepted refresh, overscan, colours and font readability.

- [ ] Add a real EDID/DDC provider if suitable display-bridge hardware is available.

- [ ] Qualify 1080p clock/sync timing on hardware before allowing persistent/auto use.

- [x] Implement demand-paged framebuffer upload and a bounded native GS command
  interface in separate generic PS2 patches; cross-build the candidate kernel.
- [x] Qualify the normal scenes of the ten existing GS/Xorg/Mesa runtime
  diagnostics on real PS2 hardware. The user confirms every test works on
  2026-10-04, recorded in
  [hardware graphics evidence](docs/graphics-hardware-validation.json).
  The SCPH-90001 model comes from the earlier user report; individual physical
  exit codes and on-console artifact hashes were not captured.
- [ ] Complete the native Xorg drawing driver and Mesa shader/GLX backend.

## P0 — Finish the real Linux 7.2.8 platform port

- [x] Preserve R5900 writeback for newly mapped pages even when their dirty
  flag is clear; test read-only-page visibility and actual compiled paths.
- [x] Pass byte offsets to `kmap_local_folio`; exercise every distinct page in
  a four-page folio rather than flushing its first page repeatedly.
- [x] Restore original CP0 synchronization in the newer interrupt skipover
  prologue; audit the actual assembled instructions.
- [x] Confirm relay-v8 GNU dynamic init, authenticated root shell and GNU
  command execution on the real SCPH-90001 (user report/photos; hashes unmeasured).
- [x] Correct the initial MDIO precondition, preserve timed-out ownership,
  use FIFO-first reset and report probe phases. Build relay-v9; actual-C
  cold-start/failure tests and actual PCSX2 DHCP/DNS/ICMP pass.
- [x] Identify the reported native SMAP failure as TX FIFO reset timeout on
  the legacy EE register path; implement IOP-owned FIFO/MAC/MDIO access with
  bounded SIF RPC and retain `ps2-smap` as the driver identity.
- [x] Use generic PS2 identifiers and filenames in all exported patches;
  check patch syntax and apply the driver/firmware/dependent recipe sequence.
- [x] Confirm native `ps2-smap` basic Ethernet on the real SCPH-90001:
  dynamic IPv4 on `eth0`, carrier up and four external ICMP replies with
  zero packet loss (user confirmation/photo; kernel hash unmeasured on console).
- [x] Confirm DNS resolution on the physical SCPH-90001: the user reports
  successful `ping gnu.org` and no messages returned by `dmesg -l err`.
- [ ] Qualify DHCP, DNS and packet delivery on other Slim revisions, Fat
  consoles with official Ethernet adaptors and early PCMCIA models.
- [ ] Measure IOP packet polling throughput, CPU cost and packet loss under
  bidirectional load; consider batching and IRQ notification after qualification.
- [ ] Exercise cable removal, PHY renegotiation, reset recovery, late RPC
  completions and concurrent ATA/USB traffic on physical hardware.
- [ ] Confirm physical UK keyboard keycode 86 and shifted bar input; preserve
  correct UK `#`, `~`, quotes and other punctuation.


- [x] Use supported 32-bit CP0 EntryHi accesses and a 32-bit region mask on
  R5900; preserve normal 64-bit behavior for other MIPS CPUs. User confirms
  physical Linux console output after this correction.

- [x] Build USB core/OHCI/PS2 OHCI, USB storage/SCSI disk, HID and IOP register
  support into the kernel; verify an empty early module manifest and no `.ko`
  payloads in the initramfs.

- [x] Serialize shared SIF DMA packet construction/launch and make INTC/DMAC
  toggle masks idempotent; exclude masked pending events from dispatch.

- [x] Prepare the IOP USB interrupt relay before controller DMA setup; retain
  outstanding RPC memory ownership and print useful wait/checkpoint diagnostics.

- [x] Prepare reused SIF/RPC receive pages before IOP DMA writes; reproduce the
  stale-reply failure and verify corrected behavior with a noncoherent cache model.

- [x] Replace cached EE firmware writes into IOP heap memory with aligned SIF
  DMA uploads; zero padded tails and keep in-flight allocations owned.

- [x] Remove synchronous relay-entry logging, increase the RPC stack to 4 KiB,
  and expose startup progress through an owned IOP mailbox. Report a stalled
  RPC once and only subsequent phase changes; exercise preemption/failure and
  mailbox lifetime in actual C tests.

- [x] Correct IRX file/BSS word alignment, include read-only data and all
  section padding in module sizes, and validate all six firmware layouts.

- [x] Exercise an intentional relay startup stall in PCSX2: phase 2 is observed
  after the linker fix, with one warning and one phase report over 90 seconds.

  Preserve the failing phase-0 baseline and exclude private probes from release.

- [x] Add named kernel symbols and a compact final Oops summary containing
  EPC/RA, BadVA/Cause, copy arguments, task and last initcall.

- [x] Call the actual reporter in PCSX2 with synthetic registers; verify summary
  values and names below the trace. Keep the private probe out of the release.

- [ ] Qualify emulator kernel exception reporting: deliberate invalid-copy and
  BUG probes currently stall before the Oops reporter, despite normal boot passing.

- [ ] Identify the physical relay-v2 memory-copy Oops caller and invalid source
  if it recurs; relay-v3 reached a loader wait instead of an available fault summary.

- [x] Confirm real SCPH-90001 USB enumeration and ext4 root mounting (v5/v7).
- [x] Confirm real SCPH-90001 GNU dynamic init and login on relay-v8.
- [ ] Confirm UK punctuation on the physical keyboard.
  Relay-v2 panicked; relay-v3 waited indefinitely in IRQ relay LOADFILE startup.

  USB enumeration and ext4 mounting are user-confirmed on v5/v7. Relay-v8 GNU dynamic init/login is user-confirmed; preserve the earlier SIF DMA firmware
  upload and phase diagnostics.

- [ ] Implement cancellable/bounded SIF RPC recovery only with proven handling
  of late DMA, callback/client lifetime and IOP module unload/reset.

- [x] Verify official 7.2.8 archive and lock its checksum.

- [x] Import the complete pinned frno7 PS2 delta, full snapshots, patches,
  file hashes and original licenses; import IOP firmware sources.

- [x] Import experimental ATA/gamepad branch driver references separately.

- [x] Apply added platform/driver files to 7.2.8; record rejected modified hunks.

- [x] Add source/configuration gates so incomplete imports cannot become a
  supposedly successful PS2 boot image.

- [ ] Resolve all relevant rejects in `kernel/port-report.json`; identify any
  non-PS2 compatibility changes that should be intentionally omitted with reasons.

- [ ] Port CPU_R5900 detection, feature overrides, boot entry, exception vectors,
  register/context preservation, o32 syscall entry and user signal contexts.

- [ ] Port LL/SC emulation and atomics, branch/delay-slot handling, multiply/divide
  emulation, FPU behavior, unaligned access, and the short-loop erratum workaround.

- [ ] Verify TLB handling, D/I cache maintenance, DMA address translation and
  coherency, IRQ routing, clocksource/clockevents, memory reservations and reboot.

- [x] Forward-port `r4kcache.h` with R5900 `sync.l`/`sync.p` and inherited index
  sequences while retaining modern exception fixups; compile and inspect assembly.

- [ ] Validate the adapted cache and DMA ordering on actual PS2 hardware.

- [ ] Update architecture Kconfig/Kbuild and compressed standalone ELF boot code;
  retain version 7.2.8 rather than copying 5.4 Makefiles wholesale.

- [x] Cross-build the selected 7.2.8 `vmlinux`, modules and compressed audit ELF;
  preserve logs and compiler identities, including the project compiler cache probe.

- [x] Restore R5900 decompressor CPU Status normalization: enable EE EIE before
  the main kernel enables interrupts; verify timer calibration and GS text in PCSX2.

- [x] Build a standalone GS stopping probe; the user observed a solid red screen
  on the real PS2, confirming that the launcher executes it and video is visible.

- [x] Build isolated stopping yellow/green/cyan compressed-kernel checkpoints;
  reproduce the cache PAL baseline byte-exact, retain BSS NOP delay slots and
  verify native 64-bit GS stores, inherited timings and 32 MiB load bounds.

- [x] Observe yellow, green and cyan on SCPH-90001 via PS2-to-HDMI; confirm that
  BSS clearing, decompression and the explicit cache handoff complete.

- [x] Observe magenta at main kernel entry. The three-sync guard candidate remains
  blank, so it does not resolve the physical boot failure.

- [x] Localize the early blank screen to unsupported doubleword CP0 EntryHi
  access; the user now observes Linux console output on the real SCPH-90001.

- [x] Embed real early-root, 16 ordered early modules and six IOP firmware files;
  verify the actual modern embedded archive and keep it below 4 MiB.

- [ ] Complete the PS2 decompressor bootstrap and review outstanding exception/TLB errata.

- [x] Clean-build the enabled Linux 7.2.8 kernel and modules with W=1,
  CONFIG_WERROR=y and KCFLAGS=-Werror; verify zero compiler, kernel-doc,
  modpost and linker warnings. See
  [the complete build report](build/images/ps2-xorg-clean-kernel/kernel-report.json).
- [ ] Run Sparse and complete Linux driver review on enabled code.
- [x] Replace cached IOP reply access with uncached word access and publish
  RPC replies after a write barrier. Cache ownership replay tests and the
  32 MiB emulator boot pass without the previous reply stall.
- [ ] Retest the corrected IOP/kernel image after a full power-off on the
  physical SCPH-90004; the earlier frozen boot does not qualify this fix.

- [ ] Boot on actual Fat and Slim PS2 consoles, record model/ROM versions, launcher,
  CPU/RAM enumeration, cold/warm boot logs and at least one successful shell login.

- [ ] Audit old baseline kernel security before distributing a reference image;
  the old kernel is not silently substituted for the target version.

Acceptance: audited 7.2.8 source, clean selected cross-build, bounded valid ELF,
and repeatable real-console cold boot with usable GNU userspace.

## P0 — Driver correctness, IOP and SIF

- [x] Preserve original source separately from local corrections.

- [ ] Integrate every enabled driver correction into the modern build and retain
  proof of cross-compilation; do not equate source presence with functioning hardware.

- [ ] Review error-pointer checks, probe rollback, MMIO map failures, IRQ/DMA races,
  locking in atomic context, module unload, firmware failure and device lifetime.

- [ ] Filter INTC cascaded pending interrupts by the enabled mask and review
  mask/unmask idempotence for its toggle-on-write registers; test masked GS/vblank
  events alongside Timer0 rather than dispatching stale disabled events.

- [ ] Complete modern SIF DMA/RPC/command transport and IOP heap/module/IRQ loading;
  add bounded timeouts and usable error reports for coprocessor faults.

- [x] Move sleeping IOP IRQ relay firmware/RPC operations into IRQ resource callbacks;
  serialize the shared client and cross-build/test callback error paths.

- [x] Build all six imported IRX modules with ps2dev, validate actual module ELF
  formats, and preserve source hashes and toolchain provenance.

- [x] Cross-build all six current DEV9/IRQ-overlay IRX modules, including bounded
  control/rearm protocols and GPIO/EEPROM/power paths for supported variants.

- [ ] Correct IOP loader residency handling: a positive load status with
  MODULE_EXIT must not make an exported library appear available. Diagnose the
  optimized firmware initializer failure and repeat ext4/SMAP boot and PAM tests.

- [ ] Validate those control, GPIO, EEPROM and IRQ paths on real Fat/Slim/PCMCIA
  hardware; retain failure, missing-device and shared ATA ownership evidence.

- [ ] Prove early module dependency ordering and firmware availability before USB
  root discovery; bound the complete uncompressed initramfs archive.

- [ ] Verify PS2 USB OHCI, USB mass storage, keyboards/mice, hotplug, transfer
  cancellation, disconnect and console input under memory pressure.

- [ ] Validate RTC/calendar, serial console, power-off/reset and ROM/sysfs reads.

## P0 — Native Ethernet must work

- [x] Locate/import GPL legacy Linux SMAP, GPL IOP SMAP, DEV9/SPEED/SMAP register
  references and their provenance/licenses. See `docs/ethernet.md`.

- [x] Build native SMAP and PHY support into the kernel; disable USB Ethernet,
  every other Ethernet vendor family and their runtime modprobe requests.

- [x] Retain DHCP/static IPv4 handling, actual interface selection and lease renewal.

- [x] Implement DEV9 detection, power/reset, GPIO/EEPROM MAC validation, shared
  ATA ownership, Slim differences and bounded early PCMCIA control paths.

- [x] Implement direct EE SMAP PIO/FIFO access with IOP-owned shared interrupt
  control, bounded SIF lease/rearm exchanges and documented ownership/order.

- [x] Implement `net_device`, NAPI RX, TX completion/backpressure, MDIO/PHY,
  multicast filtering, stats64, close/reopen and timeout recovery.

- [x] Exercise actual packet/IRQ/DEV9 handlers with independent MMIO and peer
  models under ASan/UBSan; test wrap, lengths, allocation failure and stale replies.

- [ ] Validate the complete driver on hardware, including IRQ storms, reset,
  cable removal, malformed frames, exhausted resources and simultaneous ATA use.

- [ ] Qualify PCMCIA hotplug and AIF development units; current coverage is bounded
  initialization/control code, not observed card behavior.

- [ ] Add DEV9 DMA only after independent cache/ordering/arbitration review;
  the implemented packet path uses PIO.

- [ ] Test native Fat Network Adaptor and Slim Ethernet: DHCP/renew, static IPv4,
  DNS, bidirectional packets, checksum-correct transfers and cable reinsertion.

- [ ] Measure throughput, CPU cost and packet loss; test Ethernet while ATA/USB
  storage is active and while RAM is scarce. Record results per console/chipset.

- [ ] Add IPv6 only with measured memory cost and explicit kernel/userspace support.

- [ ] Consider a separately requested USB Ethernet option later; it is intentionally
  disabled in the current native-SMAP-only system.

Acceptance: packets demonstrably travel over the console's native port on real
hardware, with stable lease renewal and recovery; a compiled driver is insufficient.

## P0 — Hardware Xorg, Mesa and TWM

- [x] Select actual Xorg, TWM, xterm, xinit/xauth, keyboard data, input drivers and fonts.

- [x] Cross-build Xorg 21.1.24, TWM 1.0.12, xterm, bitmap fonts, evdev and
  desktop helpers with the R5900 GNU toolchain; pass target ELF/library checks.

- [x] Supply classic `startx`, console authentication, disabled TCP listening,
  one small terminal and a PS2 navy/cyan TWM theme. Render an actual host
  Xvfb/TWM preview; do not count that preview as PS2 execution.

- [x] Prevent automatic fbdev/softpipe/llvmpipe/swrast substitution: require the
  native PS2 Xorg DDX at startup and keep Mesa source fetching separate from
  driver implementation. Unsupported GS commands return errors.

- [x] Add bounded, demand-paged CPU framebuffer uploads without reserving a
  complete shadow frame at console boot. Exclude mapped CPU buffers from GS
  hardware submission, including unfaulted and forked VMAs.

- [x] Implement GS fills, aligned local copies, Gouraud triangles and 16-bit
  depth testing; isolate context 2 and fence actual GS completion. Validate all
  commands before drawing, and test bounds/unsupported states under ASan/UBSan.

- [x] Check register encoding against PS2tek; explicitly select PRIM attributes
  with PRMODECONT instead of inheriting the console's attribute source.

- [x] Verify the diagnostic's rectangles, colours and depth rejection on the
  real PS2. Emulator completion counters are not pixel correctness evidence.

  The corrected CT32/Z16S photo shows two cyan rectangles and a continuous
  cyan triangle over the farther red triangle, with three manual `PASSED`
  results and console restoration on SCPH-90001. This qualifies that scene;
  The 2026-10-04 hardware report also confirms the later texture diagnostic;
  other formats/modes and OpenGL compliance remain unqualified.

- [x] Cross-build a bounded shared GS transport, native EXA fill/copy/fence
  callbacks and a Mesa draw/raster stage against real Xorg/Mesa headers.
  Pass host sanitizer tests and wrapped-ioctl R5900 callback cases.
  Export generic PS2 source patches and check clean application.

- [x] Add bounded native GS image transfers, Xorg `UploadToScreen` and Mesa
  opaque front-buffer uploads. Pass seventeen host sanitizer and R5900
  callback/packet cases, build an isolated kernel and package a diagnostic
  USB image. The user confirmed the upload scene and return to text on the
  SCPH-90001. Full desktop integration is pending.

- [x] Select progressive boot modes explicitly and verify the real kernel mode
  parser. User confirms stable 576p console from boot. Distinguish 576p from
  480p in the project PCSX2 runtime so its output tables allow all 576 lines.

- [x] Implement native Xorg DDX drawing, glyph/image uploads, window clipping,
  supported raster operations, cursor handling and VT ownership. Prevent Xorg
  from invoking CPU rasterization for unsupported rendering paths.
  Native core bitmap glyph callbacks and GC lifecycle management are
  implemented separately below; full DDX installation and Render alpha glyphs
  remain required.

- [x] Add an exclusive file-owned GS endpoint with page allocation outside
  front/font/depth reservations, cleared native offscreen surfaces, opaque
  handles, mode/font/pan invalidation and disconnect protection. Cross-build
  offscreen EXA and real Gallium resource components; pass fourteen host
  sanitizer/R5900 cases and package a separate diagnostic image.

- [x] Integrate triangles and allocated Z16/Z16S depth into the owned endpoint.
  Retain actual Mesa color/depth resources and fence shared rendering before
  EXA drawing/storage changes. Pass ten new host sanitizer/R5900 cases, export
  generic patches and build a separate image. The user confirms cyan over red,
  hidden yellow and restored text in PCSX2; full desktop integration is pending.

- [x] Add bounded native RGB texture sampling, perspective STQ, logical region
  clamp for non-power-of-two images, GS cache invalidation and Mesa sampler
  views/final draw callbacks. Add native RGB `PictOpSrc` EXA compositing with
  Mesa target restoration. Pass eleven host sanitizer/R5900 cases and export
  generic kernel/Mesa/Xorg patches. No complete GL profile is advertised.
  User confirms the PCSX2 panels and text return. Correct the diagnostic's
  mismatched trapezoid width/reciprocal depth; check planar projection in both
  triangles, pass three host and eleven R5900 cases, and preserve the earlier
  image. The user supplies an improved PCSX2 screenshot and explicitly
  confirms that the corrected checker grid is continuous.

- [x] Qualify the normal offscreen, owned color/depth and sampled texture
  diagnostic scenes on physical hardware. The 2026-10-04 user report confirms
  all tests, including independent surfaces, depth overlap and corrected
  perspective panels.
- [ ] Qualify provider removal/reset/error recovery, RGB555 and other untested
  format/state variants on physical hardware; complete double buffering,
  further sampler states and readback.

- [x] Add actual Gallium screen/context resource, surface, framebuffer, sampler,
  clear/upload/copy/flush callbacks and deferred resource/view/context lifetime.
  Hold an Xorg context lease until the full EXA callback table is restored.
  Pass six actual callback and three diagnostic cases with host sanitizers and
  R5900 Linux execution; export checked generic context patches and preserve
  earlier images. The separate PCSX2 image completes its sixth context probe
  with exit zero. Shader caps remain zero and `draw_vbo` rejects unfinished
  shader execution. The 2026-10-04 all-tests hardware report confirms the
  context diagnostic on real PS2, according to the explicit user report.

- [x] Add actual Xorg `PolyGlyphBlt`/`ImageGlyphBlt` callbacks using bounded
  native GS bitmap plans, glyph metrics, composite clipping and text background.
  Preserve Mesa targets and hold a GC context lease until callback restoration.
  Pass eight callback and three probe cases with host sanitizers and R5900
  execution; export checked generic bitmap/glyph patches and package a separate
  USB diagnostic. The seventh PCSX2 probe exits zero and the supplied screenshot
  matches the three-row pattern, including the deliberate `S`/`T` clip gap.
  The 2026-10-04 hardware report confirms this glyph diagnostic, with physical
  verification report. Full DDX installation and further formats remain open.

- [x] Manage Xorg screen/GC creation, validation, copying, clip changes and
  destruction with early private registration and bounded GC allocation.
  Route ordinary 8/16-bit core text through the native glyph callbacks; restore
  hooks around mutable EXA/damage drawing tables. Restore callbacks before
  releasing Mesa leases or closing the lower screen. Pass eight host/R5900
  lifecycle cases and three actual diagnostic cases, plus eleven host glyph
  regression cases. Export a generic GC patch and preserve the earlier image
  and source snapshot. All eight startup command checks pass in PCSX2 and the
  GC probe exits zero. The supplied text screenshot matches the deliberate
  `S`/`T` clip gap; the same pattern is used by the preceding glyph probe.
  The 2026-10-04 all-tests report confirms this GC diagnostic on real PS2;
  full DDX installation and shader/GLX work remain open.

- [x] Add all sixteen constant-source solid Boolean raster operations and RGB
  plane masks through at most two masked GS sprites, preserving alpha and
  depth storage. Add capability-gated EXA solid callbacks and an explicit
  Mesa resource adapter that fences the shared queue without changing the
  target/depth binding. Pass four kernel/packet, six backend and three actual
  probe cases under host sanitizers and R5900 execution. Export checked
  generic kernel/Xorg/Mesa logic patches and preserve the earlier images.
  All nine PCSX2 startup command checks pass and the new probe exits zero;
  the user supplies the expected grid and confirms the XOR outline appears,
  disappears and returns to text.
  The 2026-10-04 hardware report also confirms the logic diagnostic on real PS2. RGB555/partial masks, general source-pixmap
  logic operations and advertising OpenGL logic-op capabilities remain
  unqualified. Native thin solid GC
  routing is implemented separately below.

- [x] Route six actual Xorg solid GC operations (`FillSpans`, `PolyPoint`,
  `Polylines`, `PolySegment`, `PolyRectangle`, `PolyFillRect`) through a shared
  Mesa native batch and GS Boolean drawing. Preflight the full clipped request
  before fencing, support zero-width solid axis-aligned lines with correct
  endpoint/corner handling, retain RGB plane masks and report damage. Preserve
  color/depth bindings, resource references, mutable lower GC tables and lease
  cleanup ordering. Pass thirteen new host/R5900 cases and eleven GC/text
  regression cases. Export generic Xorg solid GC/Mesa batch patches and retain
  the earlier GC source snapshot and USB images. The separate solid GC image
  exercises a panel and three reversible XOR outlines. All ten PCSX2 startup
  command checks pass and the new probe exits zero; the user supplies the
  expected panel and confirms that all three outlines erase and the console
  returns. The 2026-10-04 hardware report confirms this diagnostic on the real
  PS2. The full TWM session, RGB555/partial-mask runtime pixels,
  wide/dashed lines, further tile ALUs/masks, alpha glyphs, full DDX setup and Mesa
  shader/GLX execution remain open.

- [x] Add native Xorg `CopyArea` through upstream `miDoCopy` and shared Mesa
  canonical region copies. Preflight all bounds/boxes before queue flush;
  preserve bindings, references, GC revalidation and close ordering. Handle
  overlap like `memmove`, use GS transfers for aligned bulk and one 8 KiB GS
  scratch surface for unaligned tails, without CPU destination drawing or
  pixel readback. Pass twelve new host sanitizer and thirty R5900 cases,
  including solid/GC regressions. Export checked generic copy patches and
  package a separate eleventh diagnostic using the stable 576p kernel;
  preserve earlier qualified artifacts. All eleven PCSX2 startup command checks
  pass; the copy probe exits zero, and the user confirms matching panels,
  overlap movement, clipped gap and text restoration. See [copy evidence](docs/graphics-copy-validation.json).
- [ ] Qualify `ps2-gs-copy-test` on real hardware: identical panels, overlapping
  down/right move, split right panel with a narrow clip gap, then console.
  The dated all-ten report does not qualify this newly added test.
- [x] Connect the actual Gallium `resource_copy_region` callback to the native
  GS copy adapter for RGBX/RGB555 2D resources. Validate ownership, formats,
  level/layer and the entire box before queued drawing is flushed; handle
  odd-width edges and preserve framebuffer/depth/sampler references. Pass
  seven new host/R5900 cases and 39 shared Xorg/context regressions in each
  environment. Export the generic incremental `mesa-ps2-resource-copy.patch`
  and retain the previous qualified CopyArea image and source snapshot.
  See [callback evidence](docs/graphics-resource-copy-validation.json).
- [x] Exercise the actual Gallium copy callback for an RGBX self-copy in the
  PCSX2 blit diagnostic. The observed fourth scene matches the expected move;
  retain its limited CT32 component scope in [runtime evidence](docs/graphics-blit-runtime-validation.json).
- [ ] Qualify the Gallium copy callback on physical PS2 hardware, with RGB555
  runtime pixels and existing Mesa application integration.
- [x] Add actual EXA `PrepareCopy`/`Copy`/`DoneCopy` callbacks through the shared
  Mesa GS copy batch, replacing the aligned-only route when installed. Support
  RGBX/RGB555, odd widths, self-overlap and ordered rectangle streams. Check
  native storage, ownership and each rectangle before flushing queued Mesa
  drawing; preserve no-op requests and context leases. Pass eight new host
  sanitizer/R5900 cases and sixteen shared copy regressions in each environment.
  Export `xorg-ps2-exa-copy.patch`, verify clean pinned-series compilation and
  retain an isolated source snapshot and archives. See
  [EXA copy evidence](docs/graphics-exa-copy-validation.json).
- [ ] Qualify EXA `PrepareCopy`/`Copy`/`DoneCopy` in emulator/physical pixel tests
  and the full native X server lifecycle. The separate native `CopyWindow`
  hook uses the shared Mesa batch directly and does not qualify these callbacks.
- [x] Connect Gallium `blit` to native GS nearest scaling, source flips and
  destination scissoring for matching RGBX/RGB555 resources. Reuse the copy
  batch for unscaled/overlapping transfers. Add EXA opaque `PictOpSrc` copies
  through the same public adapter, with checked pixmap storage and context
  leases. Validate geometry and supported states before flushing; preserve
  framebuffer/depth/sampler bindings and no-op queue ordering. Pass eleven
  new host sanitizer/R5900 cases and thirty regressions in each environment.
  Export `mesa-ps2-blit.patch` and `xorg-ps2-blit.patch`, check clean pinned
  series compilation and retain archives/source snapshots. See
  [blit evidence](docs/graphics-blit-validation.json).
- [x] Package `ps2-gs-blit-test` in a separate stable-576p image, using actual
  Gallium blit/resource copies and EXA composite callbacks. Pass four new
  sanitizer/R5900 lifecycle checks per environment. Run only this new test at
  startup, preserving prior images and diagnostic binaries. PCSX2 logs every
  scene and probe exit zero; the user's five screenshots match resized,
  flipped, scissored and overlapping RGBX scene panels. Keep text-only
  [runtime evidence](docs/graphics-blit-runtime-validation.json).
- [ ] Qualify the new blit/composite scene on physical PS2 hardware and RGB555
  runtime transfers. The PCSX2 diagnostic uses RGBX/CT32; qualitative scene
  review does not provide GPU readback or complete X server qualification.
- [x] Add native screen `CopyWindow` through the shared Mesa GS copy batch.
  Translate the caller's source region after success, clip to `borderClip`,
  subtract redirected pixmap origins, validate the whole request before flush,
  and preserve overlap ordering and context ownership. Retain the upstream
  Damage wrapper contract and reverse callback cleanup, without lower pixel
  drawing. Pass eleven new host sanitizer/R5900 cases and 41 shared regressions
  per environment. Export `xorg-ps2-copy-window.patch` after the blit patch;
  verify Xorg 21.1.24 compilation with COMPOSITE both enabled and disabled.
  See [CopyWindow evidence](docs/graphics-copy-window-validation.json).
- [x] Package `ps2-gs-window-test` in a separate stable-576p image with actual
  `ScreenRec.CopyWindow` and shared Mesa transfers. Pass four lifecycle checks
  on host/R5900: success, move failure, presentation failure and interruption.
  Preserve all twelve earlier binaries and images; run only this new probe at
  boot. PCSX2 executes all four scene command checks, exits zero and restores
  the text console. Retain [text-only runtime evidence](docs/graphics-copy-window-runtime-validation.json).
- [x] Visually qualify all four new RGBX `CopyWindow` scenes on PCSX2. The user
  confirms all steps and console return; four supplied screenshots match the
  identical panels, overlap edges, clipped gap and redirected backing move.
  Keep the confirmation text-only in [runtime evidence](docs/graphics-copy-window-runtime-validation.json).
- [ ] Qualify `CopyWindow` on physical PS2 and RGB555 runtime pixels, then
  exercise actual X server/Damage window moves and repaints with TWM.
  PCSX2 panel review and component models retain their component scope.
- [x] Add native `ScreenRec.PaintWindow` for solid backgrounds and borders
  through the shared Mesa Boolean batch. Resolve nested `ParentRelative`
  backgrounds, preserve None/inhibition no-ops, clip backgrounds to the child
  `clipList`, clip borders to backing bounds, convert redirected coordinates
  and report precise Damage regions. Bound plans to 256 boxes/4096 bytes;
  preflight before fencing and restore callbacks before releasing context
  leases. Pass eleven new sanitizer/R5900 cases and 27 related regressions
  per environment. Export `xorg-ps2-paint-window.patch` after the window-copy
  patch; compile against pinned Xorg with COMPOSITE enabled and disabled.
  See [window-painting evidence](docs/graphics-paint-window-validation.json).
- [x] Add native tiled background/border `PaintWindow` through an explicit
  Mesa repeat adapter. Support owned RGBX/RGB555 and non-power-of-two tiles,
  inherited ancestor alignment, original border images, redirected origins
  and precise Damage. Expand small tiles using GS texture seeding and local
  doubling, with at most 8192 logical pixel bytes of GS scratch; batch output
  into at most 64 triangles per packet. Validate the whole request and reserve
  scratch before queue flush. Pass thirteen new and 38 regression cases per
  environment, including tiny full-screen patterns and seed/expansion/output
  failure without replay. Export `mesa-ps2-tile.patch` and
  `xorg-ps2-tiled-paint.patch`; see
  [tiled-painting evidence](docs/graphics-tiled-paint-validation.json).
- [x] Package `ps2-gs-paint-test` in a separate stable-576p image, retaining
  all thirteen preceding diagnostics. Execute the actual native paint hook
  and Mesa tile/solid adapters in four scenes. Pass five host sanitizer and
  R5900 lifecycle checks, including independent scene pixels, interrupted
  cleanup and solid/expansion/presentation failures without replay.
- [x] Complete all four native painting scene command/fence checks in PCSX2
  with probe exit zero and text-mode restoration; preserve the exact run in
  [text-only runtime evidence](docs/graphics-tiled-paint-runtime-validation.json).
- [x] Review supplied PCSX2 paint panels for scenes 2–4: cyan-bordered checker,
  continuous inherited checker with patterned border, and matching front/
  backing-pixmap panels. Record qualitative verification as text only.
- [x] Confirm the solid paint scene and console return visually in PCSX2;
  the user confirms both, completing qualitative verification of all four scenes.
- [ ] Qualify window painting on real PS2 and in the actual X server/Damage
  lifecycle; retain alias/format/VRAM bounds. The earlier hardware report
  does not qualify this new paint diagnostic.
- [x] Add native tiled `GCOps.FillSpans` and `PolyFillRect` through the shared
  Mesa repeat adapter, preserving solid fill, CopyArea and glyph dispatch during
  GC revalidation. Support owned RGBX/RGB555, non-power-of-two patterns,
  drawable-relative `patOrg`, pretranslated spans and redirected backing offsets.
  Use `tile.pixel` for `tileIsPixel`, with all sixteen constant-source ALUs and
  RGB masks. Preflight at most 4096 inputs/256 clipped boxes before flush;
  keep the heap plan at 4096 bytes and destination drawing on GS.
  Pass fifteen new and 59 regression cases per environment; export
  `xorg-ps2-tiled-gc.patch` after tiled painting and compile both COMPOSITE modes.
  See [tiled GC evidence](docs/graphics-tiled-gc-validation.json).
- [x] Package `ps2-gs-fill-test` in an isolated stable-576p image, preserving
  fourteen preceding diagnostics. Exercise actual GC creation/revalidation,
  rectangle/span patterns, a shifted clipped checker, a non-power-of-two tile,
  redirected output and a pixel-tile color bar. Pass five host sanitizer/R5900
  lifecycle cases, including seed/expansion/presentation failures and SIGINT.
- [x] Complete all four native GC fill scene command/fence checks in PCSX2,
  with probe exit zero and text-mode restoration in the 32 MiB guest; preserve
  the exact image, binary, session and completion log in
  [text-only runtime evidence](docs/graphics-tiled-gc-runtime-validation.json).
- [x] Review all four supplied PCSX2 fill panels and confirm console return:
  matching rectangle/span checkers, shifted checker with intentional central
  gap, matching yellow/cyan patterns and front/backing checkers with cyan bar.
  Record the user's confirmation and panel review as text only.
- [ ] Qualify native GC fills on real PS2 and RGB555 runtime pixels, then execute
  the actual X server/Damage lifecycle. The existing physical report does not
  cover this new GC fill diagnostic.
- [x] Add `FillOpaqueStippled` to native rectangle/span GC callbacks with GC
  pattern origins, composite clipping and redirected RGBX/RGB555 output.
  Snapshot existing monochrome source bits, at most 8192 bytes, then construct
  foreground/background colour patterns with native GS fills. Reserve pattern
  and expansion storage before fencing; reuse the bounded Mesa tile path and
  keep CPU destination rendering disabled. Support GXcopy/full RGB masks and
  preserve GXnoop/zero-mask queues. Pass fourteen new and 74 regression cases
  per environment; export generic Mesa/Xorg patches and compile both COMPOSITE
  modes. See [opaque stipple evidence](docs/graphics-stippled-gc-validation.json).
- [x] Package `ps2-gs-stipple-test` with the stable kernel in a separate image,
  preserving fifteen earlier diagnostics. Pass five host sanitizer/R5900 cases
  checking four modeled scenes, native seed/expansion/presentation failures and
  SIGINT cleanup. The probe uploads no colour-pixel arrays.
- [x] Complete all four opaque-stipple command/fence scenes in PCSX2 with exit
  zero and text-mode restoration in the 32 MiB guest. Preserve exact source,
  binary, image, session and completion log in
  [text-only runtime evidence](docs/graphics-stippled-gc-runtime-validation.json).
- [x] Review all four supplied opaque-stipple PCSX2 panels: matching blue/white
  rectangle/span patterns, shifted checker with intentional gap, matching
  yellow/cyan patterns and matching front/backing panels with green bar.
  Record the user's confirmation of all four scenes and console return as text only.
- [ ] Qualify opaque stipples on physical PS2 and RGB555 runtime pixels, then
  execute the actual X server/Damage lifecycle. The earlier hardware report
  does not qualify this new stipple diagnostic.
- [x] Add driver-owned source-only depth-1 pixmaps, zeroed server-padded bits,
  EXA ModifyPixmapHeader/PrepareAccess/FinishAccess and destruction. Bound
  source pixel bytes to 1 MiB and count to 1024 per screen. Native stipple fills
  consume the owned source while EXA hides devPrivate.ptr. Keep colour targets on GS.
- [x] Preserve lower EXA/fb depth-1 GC editing callbacks and their mutable
  table/lifetime contracts while retaining native colour GC operations. Pass
  ten new plus 88 regression cases on host sanitizers and R5900; compile the
  incremental generic Xorg patch with COMPOSITE enabled and disabled.
  See [monochrome evidence](docs/graphics-monochrome-validation.json).
- [x] Package `ps2-gs-mono-test`, preserving sixteen earlier diagnostics and the
  stable kernel. Pass five host/R5900 lifecycle checks, including failure and
  interruption cleanup of driver-owned source bitmaps.
- [x] Complete all four owned-monochrome PCSX2 command/fence scenes with exit
  zero and text-mode restoration. Supplied panels match all four expected
  scenes, and the user confirms all four and console return. Retain
  [text-only runtime evidence](docs/graphics-monochrome-runtime-validation.json).
- [ ] Qualify the owned-monochrome probe on physical PS2 and RGB555 runtime pixels.
- [ ] Execute real Xorg client bitmap creation/PutImage/drawing through the
  complete EXA/fb server lifecycle. Lower editing is modeled in component
  checks; full server, physical and RGB555 runtime qualification remain open.
- [x] Add transparent FillStippled rectangles/spans with native GS one-bit runs;
  preserve zero-bit destinations, overlapping request order, all 16 Boolean
  ALUs and partial RGB masks. Snapshot at most 8192 source bytes and preflight
  work before fencing. All-zero/no-op fills preserve the queue and skip Damage.
  Pass sixteen new plus 98 regression cases on host sanitizers and R5900;
  export generic Mesa/Xorg patches with clean application and COMPOSITE on/off
  release compilation. See [transparent evidence](docs/graphics-transparent-validation.json).
- [x] Package the distinct `ps2-gs-transparent-test`, preserving seventeen
  earlier binaries and the stable kernel. Pass six host/R5900 lifecycle checks
  with independent modeled pixels, command/Boolean/presentation failures and SIGINT.
- [x] Complete all four transparent-stipple PCSX2 command/fence scenes with exit
  zero and text-mode restoration at 70.664 guest seconds. Preserve
  [text-only runtime evidence](docs/graphics-transparent-runtime-validation.json).
- [x] Visually qualify all four transparent-stipple PCSX2 scenes. Supplied panels
  match the expected red/white, blue/cyan with red gap, cyan/white with yellow
  bar, and orange/green with magenta bar scenes. The user confirms all four
  and console return. Retain text-only verification without adding images.
- [ ] Qualify transparent stipples on physical PS2 and RGB555 runtime pixels;
  execute real X server/Damage/client editing and measure fine-pattern performance.
  GXcopy/full-mask fills allow 262144 one-bit runs; Boolean/partial-mask fills
  allow 16384; work-limit rejection is explicit and has no CPU colour fallback.
- [x] Add all 16 opaque-stipple Boolean ALUs and partial RGB masks. Partition
  zero/one source runs so each covered destination pixel applies its background
  or foreground operation once, retaining overlap order. Collapse source colours
  equal under the active mask and preserve optimized GXcopy/full-mask repeat.
  Pass sixteen new plus 114 regression checks on host sanitizers and R5900;
  compile generic Mesa/Xorg patches against pinned releases with COMPOSITE on/off.
  See [opaque Boolean evidence](docs/graphics-opaque-stipple-logic-validation.json).
- [x] Package `ps2-gs-opaque-test`, preserving eighteen preceding diagnostics and
  the stable kernel. Pass six host/R5900 probe checks for independent pixels,
  first/later Boolean failures, completion-bar/presentation failures and SIGINT.
- [x] Complete all four opaque Boolean PCSX2 command/fence scenes with exit zero
  and text-mode restoration at 109.3742 guest seconds. Preserve
  [text-only runtime evidence](docs/graphics-opaque-stipple-logic-runtime-validation.json).
- [x] Visually qualify all four opaque Boolean PCSX2 panel scenes.
- [ ] Confirm opaque Boolean probe text-console return with the user (command restoration passed).
- [ ] Qualify opaque Boolean/partial-mask pixels on physical PS2 and RGB555 runtime;
  exercise actual X server/Damage/client editing and measure fine-pattern work.
  Boolean/partial-mask requests allow 16384 zero/one runs; larger requests reject
  before flushing, with no CPU colour fallback. Optimized opaque copy remains.
- [x] Add native zero-width solid lines in all octants through shared Mesa
  geometry and GS Boolean runs. Preserve exact Xorg screen bias, clipped phase,
  endpoint/cap and polyline/XOR join rules; lines use fgPixel independently of
  fillStyle. Twelve new plus 130 regressions pass per host/R5900 environment,
  using the actual pinned Xorg raster as the reference. See
  [thin-line evidence](docs/graphics-thin-lines-validation.json).
- [x] Export generic thin-line Mesa/Xorg patches after opaque Boolean stipples;
  clean apply/source equality and pinned-release compilation pass with
  COMPOSITE enabled and disabled.
- [x] Build the distinct `ps2-gs-line-test`; six independent-pixel/lifecycle
  checks pass per environment. Preserve nineteen earlier diagnostic binaries.
- [x] Complete all four thin-line PCSX2 command/fence scenes with exit zero
  and text-mode restoration at 70.0678 guest seconds; preserve the exact image,
  binary, session and completion log in
  [text-only runtime evidence](docs/graphics-thin-lines-runtime-validation.json).
- [x] Visually qualify all four thin-line PCSX2 scenes: matching fans, red
  clip gap, white XOR diamonds/green bar and magenta backing diamonds/white
  bar; user explicitly confirms text-console return. No images added to docs.
- [x] Keep the seven earlier standalone probe fixtures linkable with current
  Xorg screen-bias ownership; 33 additional host sanitizer lifecycle checks
  pass. Qualified component snapshots and saved binaries remain unchanged.
- [ ] Qualify thin lines on physical PS2 and RGB555 runtime pixels.
- [x] Add zero-width OnOffDash/DoubleDash segments, polylines and rectangles
  with FillSolid. Snapshot positive normalized dash lists and both colours;
  retain original phase when clipped, continuous phase across joins and fresh
  offsets for independent segments/rectangles. Sixteen new plus 142 regression
  checks pass per host/R5900 environment against unmodified Xorg fbSegment.
  See [dashed-line evidence](docs/graphics-thin-dashes-validation.json).
- [x] Add a shared native coloured-run batch; validate all runs before the
  original fence, preserve all sixteen Boolean ALUs and RGB plane masks,
  and bound the heap plan to 327680 bytes. No CPU colour renderer is added.
- [x] Export generic dashed-line patches after thin solid lines; clean apply,
  source equality and pinned-release compilation pass in both COMPOSITE modes.
- [x] Build the separate `ps2-gs-dash-test` and isolated image, preserving
  twenty earlier binaries/images; six lifecycle checks pass per environment.
- [x] Complete all four dashed-line PCSX2 command/fence scenes with exit zero
  and text-mode restoration at 69.5018 guest seconds; preserve exact runtime
  evidence in [the text-only report](docs/graphics-thin-dashes-runtime-validation.json).
- [x] Check eight preceding standalone probes against current production;
  39 additional host sanitizer lifecycle cases pass, with prior evidence intact.
- [x] Visually qualify all four new dashed-line scenes: matching cyan fans,
  intact red clip gap, white/yellow XOR diamonds with green bar and
  magenta/cyan backing diamonds with white bar. User confirms console return.
- [ ] Qualify dashed lines on physical PS2 and RGB555 runtime pixels.
- [x] Add a native GS save-under monochrome cursor through shared Mesa copies
  and coloured Boolean runs. Snapshot 64x64 source/mask and colours; restore
  before shared native front-buffer reads and writes, without recursive queue
  flushing. Sixteen new plus 158 regressions pass per host/R5900 environment.
  See [cursor evidence](docs/graphics-native-cursor-validation.json).
- [x] Connect Xorg pointer sprite, Damage, BlockHandler, QueryBestSize and
  CloseScreen callbacks, context lifetime and explicit suspend/resume. Failed
  GS restoration still unwinds lower callbacks; sixteen focused host checks
  revalidate cleanup. MI pointer/Damage framework execution remains stubbed.
- [x] Export generic native-cursor Mesa/Xorg patches after dashed lines;
  clean apply/source equality and pinned compilation pass in both COMPOSITE modes.
- [x] Build `ps2-gs-cursor-test`; six scene-pixel/lifecycle checks pass per
  environment. Nine earlier probes pass 45 host sanitizer lifecycle checks.
- [x] Complete the isolated cursor image PCSX2 command/fence boot with exit zero
  and text-mode restoration at 168.4982 guest seconds. Preserve exact runtime
  evidence, 21 preceding binaries and the stable kernel; see
  [the text-only runtime report](docs/graphics-native-cursor-runtime-validation.json).
- [x] Visually qualify all four supplied cursor panels: matching white/black
  arrows, moved arrows without visible trails, left copy excluding the cursor
  and cleared green/cyan panels with a white bar after suspend/resume.
- [ ] Obtain explicit user confirmation of cursor-test text-console return;
  the native restoration command and service exit already pass.
- [x] Qualify master/slave pointer ownership and empty monochrome glyph cursors
  in the real Xorg session. An empty xterm cursor hides through native GS;
  typing/backspace no longer fails X_CreateGlyphCursor with BadAlloc.
- [ ] Qualify cursor pixels and the complete input/VT lifecycle on physical
  PS2/RGB555. ARGB cursors pass 20 native Xorg and 45,056 GS pixel checks
  under CPU GS emulation. Multiple independent pointer devices and sustained
  physical cursor/input measurements remain outside this qualification.

- [x] Add native colour GC ZPixmap PutImage and SetSpans through shared Mesa
  uploads and Boolean colour runs. Preserve drawable-relative image coordinates,
  pretranslated spans, row padding, redirected origins, all sixteen ALUs,
  RGB masks and overlap order. Sixteen new plus 174 regressions pass per
  host/R5900 environment; see [client-image evidence](docs/graphics-client-images-validation.json).
- [x] Snapshot source and clipped boxes before fencing. Bound client data to
  2 MiB, output to 524288 pixels, boxes to 4096 and Boolean runs to 16384.
  Maximum driver request allocations are 2621440 bytes, excluding original
  client data and allocator overhead. No CPU colour destination drawing.
- [x] Connect native image operations to existing colour GC revalidation and
  cleanup, retaining depth-1 source editing and the front cursor guard.
- [x] Export generic client-image Mesa/Xorg patches after native cursor;
  clean apply/source equality and pinned compile pass in both COMPOSITE modes.
- [x] Build the distinct ps2-gs-image-test; six independent pixel/lifecycle
  checks pass per environment and ten earlier probes pass 51 host checks.
- [x] Complete and record PCSX2 boot of the isolated client-image diagnostic,
  preserving 22 prior binaries/images and the stable kernel. All four scene
  commands pass with exit zero and text-mode restoration at 163.0047 guest
  seconds in 32 MiB; see [runtime evidence](docs/graphics-client-images-runtime-validation.json).
- [x] All four supplied client-image panels match qualitatively; the user
  confirms all scenes and normal text-console return for the exact PCSX2
  diagnostic. Physical/pixel-by-pixel qualification remains separate.
- [ ] Qualify physical/RGB555 image pixels, actual X server/Damage,
  fine-image Boolean work and total/peak desktop RAM.

- [x] Extend native PutImage with XYBitmap foreground/background sources and
  XYPixmap descending bitplanes through shared Mesa colour fills/Boolean runs.
  Cover all ALUs/masks, server bit order, left pads 0-31, clipped/redirected
  RGBX/RGB555 and existing cursor ordering. Sixteen new plus 190 regressions
  pass per host/R5900; see [planar-image evidence](docs/graphics-planar-images-validation.json).
- [x] Snapshot bits and clipped boxes before fencing, without destination
  mapping or a colour-pixel expansion buffer. Source/run/heap limits reject
  before submission; maximum driver request allocations remain 2621440 bytes,
  excluding allocator overhead and the original client buffer.
- [x] Export generic planar Mesa/Xorg patches after client-image patches;
  clean application/equality and pinned compilation pass in both COMPOSITE modes.
- [x] Build distinct ps2-gs-planar-test; six modeled-pixel/lifecycle checks pass
  per environment and eleven earlier probes pass 57 host sanitizer checks.
- [x] Record PCSX2 boot of the isolated planar diagnostic, preserving 23 prior
  binaries/images and the stable kernel. Corrected command/fence exit zero at
  176.5307 guest seconds in 32 MiB; see [runtime evidence](docs/graphics-planar-images-runtime-validation.json).
- [x] Visually qualify all four planar-image scenes and normal console return.
  The initial run failed the backing zigzag panel despite command/fence success;
  preserve [failed-run evidence](docs/graphics-planar-images-initial-visual-failure.json)
  and repeat with full RGB-mask GXcopy using complete GS colour fills. The user
  confirms all four corrected scenes and console return; the final supplied
  front/backing zigzag panels match qualitatively.
- [ ] Qualify physical/RGB555 planar pixels, actual X server/Damage, fine-pattern
  performance and total/peak desktop RAM.

- [x] Add solid FillPolygon through shared Mesa integer scan conversion and
  GS fills/Boolean runs. Cover both fill rules, both coordinate modes, concave
  and self-intersecting shapes, clipping, redirected storage, RGBX/RGB555,
  every ALU/mask and cursor removal. Eighteen new plus 206 regression checks
  pass per host/R5900 against pinned MI pixels; see
  [polygon evidence](docs/graphics-filled-polygons-validation.json).
- [x] Bound polygon requests to 4096 vertices, 256 clips, 4194304 edge tests
  and 16384 native runs, before the original fence. In-place heap sorting
  bounds driver heap allocations to 425984 bytes, excluding allocator overhead
  and the original client buffer. No CPU colour destination drawing.
- [x] Export generic polygon Mesa/Xorg patches after planar-image patches;
  clean apply/equality and pinned compilation pass with COMPOSITE on and off.
- [x] Build distinct ps2-gs-polygon-test; six independent MI-pixel/lifecycle
  checks pass per environment. Preserve all preceding diagnostics.
- [x] Record polygon PCSX2 exit zero at 229.5788 guest seconds in 32 MiB.
  All four supplied scenes pass qualitative review and the user confirms
  console return; see
  [polygon runtime evidence](docs/graphics-filled-polygons-runtime-validation.json).
  All 63 earlier host probe checks also pass against the updated components.
- [x] Add transparent and opaque monochrome stippled polygons using shared
  Mesa geometry and native colour fills/Boolean runs. Preserve zero transparent
  bits, both fill rules, coordinates/origins, clips, backing/cursor ordering,
  all ALUs/masks and RGB555. Eighteen new plus 224 regressions pass per
  host/R5900, with six diagnostic checks per environment and 69 earlier host
  probe checks; see
  [stippled polygon evidence](docs/graphics-stippled-polygons-validation.json).
- [x] Bound source snapshots to 8192 bytes, examined pattern pixels to 1048576
  and output runs to 16384; prepare the whole request before fencing.
  Driver heap is bounded by 696320 bytes (680 KiB), excluding allocator
  overhead and original client storage. Export generic patches after solid
  polygons; both COMPOSITE modes compile against the pinned releases.
- [x] Package distinct ps2-gs-polygon-stipple-test with all 25 preceding
  diagnostics and the stable kernel preserved.
- [x] Record stippled-polygon PCSX2 exit zero at 174.9583 guest seconds in
  32 MiB. All four supplied scenes pass qualitative review and the user
  confirms normal text-console return; see
  [runtime evidence](docs/graphics-stippled-polygons-runtime-validation.json).
- [ ] Extend polygon tile-pixmap fills; qualify physical/RGB555 pixels,
  real X server/Damage, fine-pattern performance and total desktop RAM.

- [ ] Extend wide lines, patterned dash fills and arcs without CPU colour rendering; measure
  line-run performance and exercise actual X server/Damage before desktop qualification.
- [x] Extend tile-pixmap and source-pixmap copies to all sixteen ALUs and
  native RGB plane masks, including tiled polygons and overlapping copies.
  Retain request/VRAM bounds and no CPU colour destination fallback.
- [x] Extend colour-source CopyPlane to native colour and owned monochrome
  destinations, including every native plane, all ALUs, clipping and overlap.
- [x] Add opaque XRender Src/Over, nearest affine transforms, all four repeat
  modes and owned A1 masks, with pixman comparisons and native EXA callbacks.
- [x] Implement exact A8 coverage and translucent solid blending, basic
  Porter–Duff operations on opaque destinations, nearest projective colour,
  bilinear A1/A8 masks and direct/accumulated A8 glyphs. Pass 726 native Xorg
  comparisons and 90,112 native GS lookup comparisons; preserve the prior images.
- [x] Complete native core text ALUs, RGB plane masks and tiled fills through
  the GC span path, with full-request bitmap preflight and native pixel checks.
- [x] Add exact native RGBX/RGB555 bilinear colour filtering, signed convolution,
  A1/A8 convolution coverage and fast/good/best aliases. Pass 834 native Xorg
  comparisons and 168 component cases per host/R5900 environment; retain the
  previous kernel and alpha evidence and leave shipping images unchanged.
- [x] Support both red/blue orders of RGBX/RGB555 and all sixteen conversions,
  including exact filters, solids, masks and aliases. Pass 998 native Xorg
  comparisons, 294,912 native RGBA mapping comparisons and 177 component
  cases per host/R5900 environment; retain previous evidence and images.
- [x] Add GS conversion for RGB444, A4, RGB332/BGR233, both ARGB2222 orders,
  RGB565, ARGB1555/ARGB4444 and depth-16 RGB555/RGB444 in both orders. Pass
  224 cases per host/R5900 model and preserve the 18,326-check console candidate.
- [x] Implement the 38 advertised Direct XRender formats, 53 operators and
  six filter names on GS, including gradients, component alpha, read/write alpha
  maps, original-precision Saturate, ARGB cursors and geometric coverage.
  Native geometric Render and normal desktop checks pass under CPU GS emulation.
- [ ] Qualify the current 2D implementation on physical PS2 hardware and measure
  expensive arithmetic/filter workloads. The TrueColor 2D scope does not include
  indexed visuals or the unfinished Mesa OpenGL/GLX shader implementation.

- [x] Build the native Xorg DDX module and connect screen initialization,
  GC/EXA drawing, the cursor component, input and presentation. Run an actual
  Xorg/TWM/xterm integration session in 32 MiB PCSX2 at RGB555 depth 15.
- [x] Pass 273 component checks per host sanitizer/R5900 environment and
  the extended Xorg session: four scenes plus six lifecycle/input checks,
  client/server/session exit zero and console restoration. See
  [terminal integration evidence](docs/graphics-core2d-terminal-validation.json).
- [ ] Qualify the exact corrected DDX/kernel on physical hardware, including
  interactive xterm, stop/restart and VT ownership, before desktop readiness.

- [ ] Implement a Mesa PS2 Gallium/DRI backend and GLX integration for existing
  OpenGL applications, including shader/state execution and `draw_vbo` beyond
  the current resource/context foundation. Advertise only verified capabilities; reject unsupported
  shaders, formats, blending, stencil and texture states without software fallback.

- [x] Add bounded scalar MVP transformation, homogeneous clipping, viewport
  conversion and native GS triangles/Z16, with shared Xorg EXA presentation.
  All 261 host/R5900 component checks pass, including 19 new checks; six
  distinct diagnostic checks pass per environment. See
  [3D component evidence](docs/graphics-transformed-triangles-validation.json).
- [x] Package the first transformed 3D diagnostic as `ps2-gs-transform-test`,
  retaining all 26 preceding binaries and 18 earlier images.
- [x] Qualify the four fixed-angle transformed 3D scenes and text-console return
  in 32 MiB PCSX2. Probe exit zero at 209.9709 guest seconds, all four supplied
  scenes match, and the user confirms console return; see
  [3D runtime evidence](docs/graphics-transformed-triangles-runtime-validation.json).
  This test uses still scenes; continuous animation remains separate work.
- [ ] Connect verified Mesa shader/state execution and `draw_vbo` before testing
  existing OpenGL applications. Retain zero shader caps until supported paths
  are implemented and qualified.

- [ ] Implement and qualify VIF/VU transformation programs, clipping and GS
  submission coordination. Keep PATH1 and PATH3 ownership explicit; validate
  R5900 atomics rather than claiming unavailable LL/SC hardware support.

- [x] Export the native 2D Xorg and Mesa source patches with source hashes,
  clean generic-series application and COMPOSITE enabled/disabled compilation.
  The r38 exports match 95 Mesa and 140 Xorg source/header files.

- [ ] Test console/X ownership, input focus, VT switches, stop/restart, PAL/NTSC
  mode selection, cable variants and recovery after X failure.

- [ ] Harden GS/ps2fb probe readiness and DMA drain ordering, exact-font and
  proposed-mode VRAM bounds, failure cleanup and module/provider unbind lifetime.

- [ ] Measure demand-paged allocations and GS depth/font reservations through
  mode changes; choose desktop resolution/depth from measured memory use.

- [ ] Verify Xorg sees the advertised pixel layout (GS 16-bit RGB555 is not RGB565),
  physical stride, acceleration limits and actual mode timings.

- [ ] Start TWM with USB keyboard/mouse on real hardware and measure kernel + shell
  + Xorg + TWM + xterm memory without OOM or permanent swap thrashing.

## P1 — GNU userland and reproducible build completion

- [x] Remove BusyBox and its udhcpc configuration/hooks from the current build;
  reject residual binaries/applet links in GNU images. Select dhcpcd 10.5.2.

- [x] Use dhcpcd's immediate background mode and DNS-only event hook, retaining
  GNU Bash and GNU command-line tools.

- [x] Cross-build patched dhcpcd 10.5.2 with privilege separation, rebuild the
  GNU-only USB image, and observe automatic lease, default route, DNS and rebind
  through native SMAP in the actual Windows guest.

- [ ] Qualify DHCP renewal/expiry/deconfiguration and cable recovery on physical
  native SMAP hardware; successful emulator acquisition is separate evidence.

- [x] Cross-build signed upstream kbd 2.10.0 `loadkeys` and the UK map/includes;
  verify host/R5900 parsing byte-exact and UK punctuation, then load before login.

- [x] Select Xorg `gb` for the English UK keyboard; desktop packages cross-build,
  but a physical Xorg input session remains pending.

- [x] Read back all thirteen tested UK console symbols with `KDGKBENT` in the
  latest emulator image; user confirms correct emulator keys.

- [ ] Verify an actual UK USB keyboard on real hardware, including the ISO backslash key.

- [x] Implement checksum-locked GNU binutils/GCC/glibc bootstrap and external Linux
  compiler checks; separate Linux compiler from ps2dev SDK/newlib compiler.

- [x] Add pinned Buildroot backend with R5900 architecture support, GNU utility
  selection and explicit `base`, `desktop`, `full` profiles.

- [x] Require GNU core programs rather than silently accepting BusyBox replacements.

- [x] Enforce the requested job count in Buildroot helper bootstraps as well as Make.

- [x] Implement optional full native GNU development suite and runtime library closure checks.

- [x] Complete the Linux/glibc C/C++ compiler build and real cross-link/ELF checks.

- [x] Build verified GCC 16.2.0, binutils 2.47 and glibc 2.44 in a fresh prefix,
  preserving the older compiler and its markers/artifacts; check R5900 ABI/errata,
  actual 5.4.221 syscall headers and four dynamic/static C/C++ smoke cases.

- [x] Apply the guarded GNU libatomic test-and-set export repair; retain generic
  `-mno-llsc` runtime flags and explicit glibc `-mllsc` atomics without disabling
  glibc assertions. PS2 kernel trap emulation still requires hardware acceptance.

- [x] Rebuild the real early-root and integrated built-in SMAP kernel with GCC16;
  retain separate compiler/configuration/module and firmware provenance.

- [ ] Execute ABI/erratum test programs on PS2; rebuild with 7.2.8 headers once
  the platform port passes.

- [x] Build all 38 base target packages; preserve source/build provenance and
  validate 310 Linux ELF files, required GNU commands and loader/library closure.

- [x] Build all 38 latest-GNU base packages with 32 host dependencies; verify 78
  current console providers and run eight direct GNU R5900 Linux-user smoke tests.

- [x] Correct Bash/ed C23 compatibility and Nano pointer truth tests with scoped
  recipes; preserve ncurses ABI, checked source hashes and prior package stamps.

- [x] Preserve the historical real base runtime and six imported IOP modules in
  checked 64 MiB ext2/tar artifacts; never relabel those as the latest build.

- [x] Finish the new firmware-equipped latest-GNU ext4 image, verify overlay,
  private password provisioning, offline filesystem check and installed modules;
  retain separate hashes from all older media.

- [x] Build the desktop and full native suite and validate commands, libc,
  threads, atomics and dynamic loading on R5900 Linux-user; retain source,
  recipe and ELF hashes. Physical qualification remains separate.

- [ ] Run GNU functional smoke tests on hardware: shell pipelines, coreutils,
  find/grep/sed/awk, archive/compression, editors, filesystems and networking.

- [x] Test native GCC/G++, binutils and Make on small dynamic/static programs
  with 32 MiB RAM and swap in the emulator; threads, atomics, soft float and
  C++ exceptions pass. [Execution evidence](build/userland/native-sdk-32m/result.json)
  records the scope. Keep cross-compilation the practical default for larger
  builds; physical compiler qualification remains separate.

- [x] Build native GCC/G++ 16.2.0 and binutils 2.47 with matching glibc 2.44
  headers, libraries and SDK objects; retain the compiler provenance in
  [the native compiler report](build/userland/native-compiler/build.json).
- [x] Verify the native GCC version on R5900 and supply default R5900,
  o32, soft-float, short-loop workaround and no-LL/SC compiler specs.
- [x] Put the active package recipes and checksum-locked source archives under
  `userland/package` and `userland/sources`, with a package index. The latest
  native tool builder consumes each package's `recipe.json` directly.
- [x] Verify current stable GNU tool sources using upstream signatures;
  verify GDB 18.1 against the publisher's SHA512 manifest and pin the other
  upstream HTTPS archives, including Python 3.14.8. Source locks live in
  `configs/native-tools-latest.json` and `toolchain/source-lock.json`.
- [x] Build the complete latest native suite, including GNU Screen, Wget,
  Lynx, GDB, Python 3.14.8, libffi 3.8.0 and the Flex executable; validate
  installed versions and the [stripped runtime](build/userland/native-suite-runtime10.json).
- [x] Provide one documented full-userland build command using the existing
  toolchain, work directory, staging, profile and job-count settings.
- [x] Safely strip the assembled userland while preserving shared-library
  exports, SDK symbols, hardlinks, permissions and hashed firmware; qualify
  dynamic/static C/C++ builds and runtime behavior after stripping. The
  [ABI audit](build/userland/full-userland-ABI.json) also checks SDK archive members.
- [x] Produce and offline-check the clean 1 GiB personal image with the complete native
  development suite and disk-backed swap, normal login and manual `startx`;
  exclude diagnostic binaries, automatic test services and prior test logs.
  [Image metadata](build/images/ps2-linux-development-clean-1g/development-image.json)
  records the exact disk and zero-warning kernel hashes; physical boot remains open.
- [ ] Finish the final typing/shutdown log for the latest production-builder
  DDX and boot the exact clean personal image. The latest visual run was
  user-confirmed; the emulator closed before its final record. Earlier extended
  typing/console-return qualification is retained separately.

- [ ] Verify packages still fit their disk budget and no unused static libraries,
  locales, debug symbols or documentation inflate the default image.

- [ ] Add signed source/release verification and a deliberate lock-update workflow.

- [ ] Add CI for source integrity, configuration resolution, cross-builds and ELF
  runtime checks. Run meaningful hardware tests outside CI with archived logs.

## P1 — 32 MiB memory, root filesystem and boot usability

- [x] Import complete official SysVinit source under `userland/sysvinit`, wire
  its local build/install to `/sbin/init`, and integrate authenticated runlevel/service handling.

- [x] Keep the full GNU system on disk; implement a small early-root helper with
  module loading, root wait, API mount handoff and initramfs reclamation before SysVinit.

- [x] Check IOP firmware format and recorded hashes separately from GNU userspace ABI.

- [x] Bound /run, /tmp and /dev/shm; keep desktop/remote daemons opt-in; activate
  existing swap only, without formatting media.

- [x] Make ext4 the default with 4 KiB blocks, a bounded journal and explicit
  features; retain ext2 only as a historical reproduction option.

- [x] Assemble PAM-sensitive metadata on native Linux storage before publication
  to Windows; preserve securetty/shadow/setuid permissions and validate policy closure.

- [x] Provide locked defaults plus explicit password-hash provisioning for console login.

- [x] Observe real initramfs -> USB disk-root -> GNU SysVinit under the patched
  Linux interpreter with early modules/firmware and the preserved GNU14 system.

- [x] Test real-kernel PAM with the fixed native-filesystem permissions: wrong
  password returns 7, correct password 0, account/session checks 0.

- [x] Repeat emulator root handoff with the GNU/ext4/built-in-USB/SMAP image.

- [x] Confirm authenticated interactive root login on the SCPH-90001.
- [ ] Repeat authenticated login on other real Fat and Slim models.

- [ ] Support persistent filesystem UUID root selection and late USB enumeration;
  the current dual-partition hardware image selects `/dev/sda2` and requires a
  controlled storage layout.

- [ ] Measure boot peak, idle and desktop resident RAM; report actual values, not
  assumed budgets. Target a useful console shell with free memory for work.
  Verify the complete Xorg/Mesa/TWM/xterm session and a supported OpenGL
  application on 32 MiB hardware with swap disabled, including start/exit peaks.
  Keep the reported roughly 7 MiB console baseline separate from desktop usage.

- [ ] Evaluate read-only compressed system + writable state without excessive RAM
  usage; measure page-cache tradeoffs and disk/USB throughput.

- [ ] Provide optional preconfigured swap image creation on a regular host file,
  installation instructions, durability guidance and recoverable filesystem checks.

- [x] Prepare a bounded regular-file FAT32-plus-root hardware disk with root on
  `/dev/sda2`, preserving source inputs and never automatically writing a device.

- [ ] Validate real-console storage layout, launcher access and ext4 partition
  discovery; filesystem-only `rootfs.ext4` and whole-disk images have distinct uses.

- [ ] Test power loss, corrupted root, absent modules/firmware, wrong disk and
  full temporary filesystem; leave a useful boot log and avoid PID 1 exiting.

## P1 — Emulator testing and boot debugging

- [x] Build checksum-pinned frno7 R5900 Linux-user QEMU with exact submodule
  provenance; require the R5900 CPU instead of generic MIPS substitution.

- [x] Execute eight actual GNU/arithmetic smoke checks with per-program timeouts;
  preserve output, source/program and staged loader/library hashes.

- [x] Add a disposable partitioned USB image, validated standalone ELF/preset,
  PCSX2 launcher and stage-by-stage procedure in `docs/emulator.md`.

- [x] Test disk-copy source preservation, malformed ELF rejection, RAM/address
  bounds, fresh-session requirements and emulator failure reporting.

- [x] Detect actual PCSX2 CLI capabilities, support 2.6.3 through an isolated
  portable runtime/profile, and pass the installed executable's `-testconfig`.

- [x] Run the audit ELF in PCSX2 with Null audio, preserve ELF/early Linux 7.2.8
  output and 8192-page memory detection; retain unsupported CP0/cache notices.

- [x] Trace the blank screen to missing EE EIE and modular ps2fb; build the
  corrected console audit ELF, retain serial output and verify readable GS text
  in PCSX2 2.6.3 at 576×460 PAL (72×28 characters).

- [x] Replace the console audit's empty initramfs with real early-root, USB
  module dependencies and IOP firmware; retain a small archive-only console device.

- [x] Make verbose PCSX2 file logging opt-in; preserve guest serial output in normal logs.

- [x] Implement and compile a versioned interpreter patch distinguishing invalid
  pages from refill misses; the real `/init` advances beyond the first demand fault.

- [x] Restore the pinned R5900 RDHWR/SQ thread-pointer exception handler and
  shared declaration; compile it and exercise it with real static glibc `/init`.

- [x] Trace the glibc futex fatal to omitted o32 time32 syscalls; enable and require
  `CONFIG_COMPAT_32BIT_TIME`, verify linked syscall 4238 and the guest return value.

- [x] Fix silently skipped LL/SC opcodes by raising reserved-instruction exceptions;
  the real early-root program now loads IOP, USB, storage and input drivers.

- [x] Complete virtual SCSI/partition discovery and USB root mount after bounded
  OHCI/control/bulk traces; no physical USB stick is required for emulator tests.

- [x] Implement and cross-build interpreter ASID/V/D translation, COW faults,
  privilege/fetch contexts, host-access isolation and explicit BIOS-HLE opt-out.

- [ ] Qualify the entire emulator MMU/cache/device behavior and game compatibility;
  successful Linux bring-up alone does not validate every path.

- [x] Observe real early-root, USB root mount and GNU SysVinit, then prove PAM
  password/account/session behavior with the preserved GNU14 staging.

- [ ] Verify an authenticated interactive shell on the latest system and record
  exact emulator/kernel/rootfs/configuration hashes and guest output.

- [ ] Add guest fork/exec/thread/signal/atomic tests once full system execution
  is available; Linux-user syscall translation does not validate kernel traps.

- [ ] Add native SMAP emulation tests for the actual implemented Linux/IOP driver;
  repeat Ethernet, graphics, DMA and RAM acceptance on real Fat/Slim consoles.

## P2 — Remaining PS2 devices and full feature coverage

- [ ] Complete/test expansion-bay ATA, DMA, disk geometry and partition compatibility;
  do not overwrite PS2 game/APA partitions automatically.

- [ ] Complete/test gamepad input, hotplug, button/axis mapping and rumble.

- [ ] Implement/test memory card block/filesystem access and failure recovery.

- [ ] Implement/test CD/DVD support with appropriate lawful firmware/data sources.

- [ ] Implement/test SPU2 audio, mixer, DMA, playback/capture and resource limits.

- [ ] Expose GS/VU/IPU features only with defined APIs, ownership, bounds and tests;
  distinguish these hardware capabilities from required standard Linux device APIs.

- [ ] Evaluate PS2 camera, USB audio, Wi-Fi and additional USB devices as optional
  modules; keep firmware packages and heavier background services out of base.

- [ ] Maintain a model/ROM/peripheral compatibility matrix with unsupported cases.

## Release acceptance checklist

- [ ] Exact 7.2.8 source revision + complete port patch series + compiler/IRX hashes.

- [ ] Clean reproducible selected builds; GNU/Xorg packages and legal-info available.

- [ ] Real Fat and Slim boot logs, RAM/disk sizes and native Ethernet test evidence.

- [ ] Working TWM desktop with tested GS output and input devices.

- [ ] Documentation matches shipped behavior; no unresolved stub reports success.

- [ ] Source archive, image checksums, release notes and known limitations published
  only after the corresponding hardware acceptance criteria pass.
