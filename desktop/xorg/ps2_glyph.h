/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_GLYPH_H
#define PS2_XORG_GLYPH_H
#include "ps2_context_bridge.h"
#include "gcstruct.h"
struct ps2_glyph_gc {
    GCPtr gc;
    const GCOps *original;
    GCOps ops;
    struct ps2_surface_exa *exa;
    struct pipe_context *context;
    Bool native_spans;
};
/* Register during DDX setup, before the first server GC allocation. Xorg
 * cannot enlarge GC private storage after GCs already exist. */
Bool ps2_glyph_register_private(void);
/* Zero-initialize and retain this per-GC object through GC destruction.
 * DDX: detach before ValidateGC or DestroyGC; attach after ValidateGC and
 * after EXA's GC setup. No additional ops wrapper may be inserted while
 * attached. A context lease keeps the GS owner alive until detach.
 * Core bitmap text only; Render alpha glyphs remain a separate operation.
 */
Bool ps2_glyph_gc_attach(GCPtr,struct ps2_glyph_gc *,struct ps2_surface_exa *,struct pipe_context *);
void ps2_glyph_gc_detach(struct ps2_glyph_gc *);
#endif
