/* SPDX-License-Identifier: MIT */
#ifndef PS2_BLIT_EXA_H
#define PS2_BLIT_EXA_H
#include "ps2_texture_exa.h"
#include "ps2_blit.h"
#include "ps2_composite.h"
#include "ps2_coverage.h"
#include "ps2_format.h"
struct ps2_blit_exa {
    ScreenPtr screen;
    struct ps2_surface_exa *exa;
    struct ps2_surface_client *gs;
    struct pipe_context *context;
    Bool (*CheckComposite)(int, PicturePtr, PicturePtr, PicturePtr);
    Bool (*PrepareComposite)(int, PicturePtr, PicturePtr, PicturePtr, PixmapPtr, PixmapPtr, PixmapPtr);
    void (*Composite)(PixmapPtr, int, int, int, int, int, int, int, int);
    void (*DoneComposite)(PixmapPtr);
    PixmapPtr source_pixmap, destination_pixmap;
    struct ps2_surface source, destination;
    struct ps2_picture_sample sample;
    struct ps2_picture_bitmap mask;
    unsigned over;
    uint32_t solid,solid_mask;
    struct ps2_gradient_stop *gradient_stops[2];
    struct ps2_coverage_surface coverage_source,coverage_destination,coverage_mask;
    struct ps2_format_surface format_source,format_destination,format_mask;
    struct ps2_format_surface alpha_format[3];
    struct ps2_coverage_surface alpha_coverage[3];
    struct ps2_format_alias destination_alias;
    PicturePtr alpha_pictures[3],alpha_maps[3];
    PixmapPtr alpha_pixmaps[3];
    Bool alpha_dispatch;
    struct ps2_alpha_source coverage;
    struct ps2_alpha_source source_coverage;
    Bool coverage_colour_source;
    Bool masked;
    Bool attached, prepared;
};
/* Install after the context/texture EXA bridge, before exaDriverInit. Native
 * RGB/ARGB colour, owned A1/A8 sources/destinations and solid sources support
 * transforms, repeats and nearest/bilinear/convolution. CPU processing is
 * limited to geometry and raw client transfers. Sampling, coverage and colour
 * arithmetic execute on GS.
 * Restore these callbacks before the outer context bridge. One submission
 * thread; retain screen state and prepared pixmaps through DoneComposite. */
Bool ps2_blit_exa_prepare(ScreenPtr, struct ps2_blit_exa *, struct ps2_surface_exa *, struct pipe_context *);
void ps2_blit_exa_cleanup(ScreenPtr, struct ps2_blit_exa *);
#endif
