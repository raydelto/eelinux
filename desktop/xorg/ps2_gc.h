/* SPDX-License-Identifier: MIT */
#ifndef PS2_GC_H
#define PS2_GC_H
#include "ps2_glyph.h"

struct ps2_gc_private;
struct ps2_gc_screen {
    ScreenPtr screen;
    struct ps2_surface_exa *exa;
    struct pipe_context *context;
    CreateGCProcPtr CreateGC;
    CloseScreenProcPtr CloseScreen;
    struct ps2_gc_private *gcs;
    const GCOps *solid_ops;
    const GCOps *copy_ops;
    const GCOps *fill_ops;
    const GCOps *image_ops;
    const GCOps *plane_ops;
    const GCOps *polygon_ops;
    const GCOps *geometry_ops;
    unsigned count;
    Bool installed;
};
/* Zero-initialize screen-owned state. Install after EXA/damage setup, before
 * any server GC allocation: prepare registers every native GC private key.
 * The screen holds a lease; validated GCs retain additional glyph leases.
 * Cleanup restores
 * screen/GC callbacks before releasing them and before lower CloseScreen.
 * Additional owners of CreateGC/CloseScreen must unwrap before cleanup.
 * Single submission thread; at most 1024 managed GCs. This component does not
 * supply screen initialization, unsupported raster operations or an X DDX. */
Bool ps2_gc_screen_prepare(ScreenPtr, struct ps2_gc_screen *,
                           struct ps2_surface_exa *, struct pipe_context *);
void ps2_gc_screen_cleanup(struct ps2_gc_screen *);
/* Optional native solid hooks. Install/replace only on the submission thread;
 * the table must outlive its registration. All six callbacks are required.
 * NULL restores lower dispatch in every currently validated GC. */
Bool ps2_gc_screen_set_solid_ops(struct ps2_gc_screen *, const GCOps *);
/* Optional native CopyArea table; NULL restores lower dispatch. */
Bool ps2_gc_screen_set_copy_ops(struct ps2_gc_screen *, const GCOps *);
/* Optional FillSpans/PolyFillRect override, above native solid dispatch.
 * Both callbacks are required; NULL restores solid/lower dispatch. */
Bool ps2_gc_screen_set_fill_ops(struct ps2_gc_screen *, const GCOps *);
/* Optional native PutImage/SetSpans override, for colour GCs only. */
Bool ps2_gc_screen_set_image_ops(struct ps2_gc_screen *, const GCOps *);
Bool ps2_gc_screen_set_plane_ops(struct ps2_gc_screen *, const GCOps *);
/* Optional native solid FillPolygon; NULL restores lower dispatch. */
Bool ps2_gc_screen_set_polygon_ops(struct ps2_gc_screen *, const GCOps *);
/* MI geometry dispatch to the native span/rectangle backends. */
Bool ps2_gc_screen_set_geometry_ops(struct ps2_gc_screen *, const GCOps *);
#endif
