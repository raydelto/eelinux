/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_FILL_H
#define PS2_XORG_FILL_H
#include "ps2_gc.h"
struct ps2_fill_screen {
    struct ps2_gc_screen *gc;
    struct pipe_context *context;
    ScreenPtr screen;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Install after native GC/solid setup. Override only FillSpans/PolyFillRect;
 * solid fills retain the native solid hooks, and points/lines are unaffected.
 * Distinct owned RGBX/RGB555 tiles use Mesa's native repeat adapter for GXcopy
 * with full RGB masks; GXnoop/zero masks preserve pending work. tileIsPixel
 * uses native Boolean solid drawing with all sixteen ALUs and RGB masks.
 * FillOpaqueStippled reads an existing server-format 1bpp bitmap <=256x256;
 * GXcopy/full RGB masks construct its foreground/background pattern on GS.
 * Transparent and opaque stipples support all Boolean ALUs and RGB masks
 * through bounded native source runs. Zero transparent bits preserve pixels;
 * opaque runs partition foreground/background without repeated ALUs per pixel.
 * Pattern origin is drawable origin + patOrg, independent of miTranslate;
 * FillSpans coordinates are already translated. Redirected origins are removed.
 * At most 4096 inputs; all geometry is checked before drawing. Complex
 * clips and clipped output use batches of 256 boxes and a 4096-byte heap plan.
 * No CPU destination drawing. Cleanup before solid/GC teardown; later screen
 * wrappers must unwrap first. State and tables outlive their registrations. */
Bool ps2_fill_screen_prepare(ScreenPtr, struct ps2_fill_screen *, struct ps2_gc_screen *);
void ps2_fill_screen_cleanup(struct ps2_fill_screen *);
#endif
