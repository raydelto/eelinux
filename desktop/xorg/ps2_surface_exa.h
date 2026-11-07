/* SPDX-License-Identifier: MIT */
#ifndef PS2_SURFACE_EXA_H
#define PS2_SURFACE_EXA_H
#include "xorg-server.h"
#include "scrnintstr.h"
#include "exa.h"
#include "ps2_gs_surface.h"

struct ps2_surface_exa {
    struct ps2_surface_client *gs;
    Bool (*coverage_upload)(struct ps2_surface_exa *,PixmapPtr,int,int,int,int,char *,int);
    void *coverage_private;
    ExaDriverRec driver;
    uint32_t color, source;
    struct ps2_exa_pixmap *monochrome;
    size_t monochrome_bytes;
    unsigned monochrome_count;
};
struct ps2_mono_source {
    const unsigned char *bits;
    size_t bytes, stride;
    unsigned width, height;
};
struct ps2_alpha_source {
    const unsigned char *bits;
    size_t bytes, stride;
    unsigned width, height, bits_per_pixel, depth;
};
/* Owned raw protocol bytes. Pixel decoding, drawing and packing run on GS. */
int ps2_surface_exa_alpha_source(PixmapPtr, struct ps2_alpha_source *);
/* Server protocol bytes bound to a zero-sized scratch header; source only. */
int ps2_surface_exa_borrowed_source(PixmapPtr,struct ps2_alpha_source *);
struct ps2_raster_target;
/* Mutable view only for GS-encoded coverage publication into owned storage. */
int ps2_surface_exa_raster_target(PixmapPtr,struct ps2_raster_target *);
/* 1: valid owned source-only bitmap, 0: not owned here, -1: invalid owned
 * header. EXA hides devPrivate.ptr between accesses; this view remains owned
 * by the pixmap and must be consumed synchronously before its destruction. */
int ps2_surface_exa_mono_source(PixmapPtr, struct ps2_mono_source *);
/* EXA storage/drawing callbacks for the native DDX's owned GS session. */
Bool ps2_surface_exa_prepare(ScreenPtr, struct ps2_surface_exa *, struct ps2_surface_client *);
#endif
