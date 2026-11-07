/* SPDX-License-Identifier: MIT */
#ifndef PS2_RENDER_GEOMETRY_H
#define PS2_RENDER_GEOMETRY_H
#include "ps2_blit_exa.h"
struct ps2_render_geometry {
    ScreenPtr screen;
    struct ps2_blit_exa *blit;
    TrapezoidsProcPtr trapezoids;
    TrianglesProcPtr triangles;
    AddTrapsProcPtr traps;
    AddTrianglesProcPtr add_triangles;
    RasterizeTrapezoidProcPtr rasterize;
    CloseScreenProcPtr close;
    Bool installed;
};
Bool ps2_render_geometry_prepare(ScreenPtr,struct ps2_render_geometry *,struct ps2_blit_exa *);
#endif
