/* SPDX-License-Identifier: MIT */
#ifndef PS2_COPY_EXA_H
#define PS2_COPY_EXA_H
#include "ps2_context_bridge.h"
#include "ps2_copy_batch.h"
struct ps2_copy_exa {
    ScreenPtr screen;
    struct ps2_surface_exa *exa;
    struct ps2_surface_client *gs;
    struct pipe_context *context;
    Bool (*PrepareCopy)(PixmapPtr, PixmapPtr, int, int, int, Pixel);
    void (*Copy)(PixmapPtr, int, int, int, int, int, int);
    void (*DoneCopy)(PixmapPtr);
    PixmapPtr source_pixmap, destination_pixmap;
    struct ps2_surface source, destination;
    uint32_t plane_mask;
    unsigned alu;
    Bool attached, prepared, noop;
};
/* Install after the context/texture/logic bridges, before exaDriverInit.
 * Native RGBX/RGB555 copies; all ALUs/masks with TEXTURE_TRANSFER, otherwise
 * the original full-mask GXcopy path. GXnoop/zero RGB mask are no-ops. One submission
 * thread; pixmaps and this screen-owned state must live through DoneCopy.
 * Restore these three callbacks before releasing the outer context bridge. */
Bool ps2_copy_exa_prepare(ScreenPtr, struct ps2_copy_exa *, struct ps2_surface_exa *, struct pipe_context *);
void ps2_copy_exa_cleanup(ScreenPtr, struct ps2_copy_exa *);
#endif
