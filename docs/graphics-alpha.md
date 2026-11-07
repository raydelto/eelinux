# Native Xorg/Mesa alpha extension

This page records the alpha revision. The subsequent
[filter extension](graphics.md) adds native RGBX/RGB555 bilinear and convolution
colour filtering, with 834 native Xorg comparisons. Its
[validation record](graphics-filters-validation.json) preserves the newer
module and tests separately from the alpha evidence below.

The 2026-10-13 source extends the shared PS2 GS drawing backend with exact A8
coverage blending, translucent solid sources, the basic Porter–Duff operators
on opaque destinations, projective nearest sampling, and bilinear A1/A8 masks.
Core text now observes all sixteen Boolean ALUs, RGB plane masks and tiled fills.
Direct and accumulated A8 Render glyphs, including overlapping glyphs, are covered.

The production kernel and Xorg module pass 726 native X11 comparisons at depth 15
in the 32 MiB PCSX2 guest. Of these, 392 compare Render results with pixman. The
separate native lookup diagnostic passes 90,112 pixel comparisons. Xorg and the
client both exit successfully with zero client errors. The client oracle also
passes 726 comparisons at each of depths 15 and 24 under Xvfb. Xvfb and the
wrapped-packet component fixtures are separate from native GS execution.

See [the validation record](graphics-alpha-validation.json) for the final host,
R5900, patch, source and artifact hashes. The preceding
[plane/composite record](graphics-plane-composite-validation.json) remains a
record of its earlier revision.

## Supported scope

| Picture or operation | Current path |
| --- | --- |
| Native RGBX (`x8b8g8r8`) and RGB555 (`x1b5g5r5`) | GS colour storage and drawing |
| Premultiplied solid sources, including translucent colours | Owned 1x1 GS source and constant alpha |
| Clear, Src, Dst, Over, OverReverse, In, InReverse, Out, OutReverse, Atop, AtopReverse, Xor, Add, Saturate | Exact reduction for opaque RGB destinations |
| Nearest affine/projective colour sampling; None, Normal, Pad, Reflect repeat | CPU coordinates and GS colour sampling |
| A1/A8 masks with nearest or bilinear sampling | Owned source coverage, followed by GS colour arithmetic |
| A1/A8 Render targets and glyph-mask accumulation | CPU edits to owned coverage storage |
| Core text with all ALUs, partial RGB masks, and tiled fills | Bitmap geometry and native GC span drawing |

CPU coverage editing is explicit. It does not expose a colour pixmap or map the
colour framebuffer. RGB source sampling, destination blending, saturation and
final RGB555 conversion execute on GS. Compositing performs no RGB readback.
Core client `GetImage`/`GetSpans` continue using their separate read-only protocol
reply path. Software Mesa rendering is not installed.

The user's completion target is **all XRender formats and filters**. This revision
does not meet that target. Varying ARGB source/destination alpha, other colour
formats and channel layouts, component-alpha masks, colour bilinear/convolution
filters, gradients, extended blend operators, and full alpha-map semantics still
need native implementations. Mesa shader/OpenGL/GLX execution remains unfinished.
Unsupported colour operations retain the existing fail-closed behaviour.

## GS arithmetic and ownership

`PS2GS_CHANNEL_LUT` uses three separate session-owned surfaces: a 64x32 CT32
source, a 64x32 CT32 destination, and a 16x16 CT32 palette. The request names
handles, origin-rectangle dimensions and an RGB bit mask. It accepts no VRAM
address or raw register. The provider rejects aliases, depth surfaces, stale or
foreign handles, invalid dimensions and non-RGB masks before submitting work.
It preserves alpha and unselected RGB bits. DMA failures stop the session and
are never replayed as drawing requests.

CT32 byte reinterpretation as PSMT8 makes each colour channel an index into the
palette. The swizzle mapping is derived from Sony's
[32bit Colour Channel Shifting column diagrams](https://ps2linux.no-ip.info/playstation2-linux.com/download/p2lsd/32bit_colour_channel_shifting.pdf).
The CSM1 palette swaps index bits 3 and 4. Packet fixtures cover every origin
rectangle from 1x1 to 64x32 and all three RGB channels, including odd edges.

The palette encodes exact rounded multiplication by an eight-bit coverage
value. A second lookup weights the destination, and a saturating texture
transfer combines the two contributions. RGB555 expansion includes pixman's
bit replication before arithmetic; the final native transfer quantizes to
five-bit channels. `PS2GS_TRANSFER_ADD_CLAMP` supplements the existing wrapping
add and subtract used by Boolean raster operations.

Coverage storage remains limited to 1 MiB and 1024 pixmaps, shared by A1 and A8.
The composite plan is limited to 1,048,576 pixels: 4 MiB for nearest coordinates,
plus up to 1 MiB for coverage. Source colour textures are at most 1024x1024.
Alpha arithmetic reserves 64 KiB of GS VRAM, including the palette. These are
individual allocation limits. They do not establish a peak desktop RAM budget
or interactive alpha-rendering performance. Saved meminfo is taken after Xorg
exits. Physical hardware qualification remains pending.

## Build and artifacts

The production module is built by `scripts/native_xorg.py`; diagnostic clients
are compiled separately. `scripts/graphics_alpha_kernel.py` clones the previously
verified kernel source and builds the typed lookup extension with `W=1` and
`KCFLAGS=-Werror`. The compressed `vmlinuz` ELF is the boot artifact; the raw
`vmlinux` is not the validated emulator boot entry.

Apply `kernel/patches/ps2-gs-lut.patch` after the GS batch patch. Apply the Mesa
and Xorg `*-ps2-alpha.patch` deltas after their existing plane/composite series.
The exported production files are checked for source equality and compiled
with Xorg COMPOSITE both enabled and disabled.

`build/graphics-alpha-20261013` holds the production module, matching compressed
kernel, separate diagnostic clients and verification logs. The module requires
the new kernel capability for alpha arithmetic. Existing shipping USB/DVD images
have not been rebuilt with this extension.
