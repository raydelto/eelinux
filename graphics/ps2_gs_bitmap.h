/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_BITMAP_H
#define PS2_GS_BITMAP_H
#include "ps2_gs_render.h"
#include <stddef.h>
struct ps2_bitmap_box { int32_t x1,y1,x2,y2; };
struct ps2_bitmap_glyph {
    int32_t x,y;
    unsigned width,height;
    const unsigned char *bits;
    size_t bytes,stride;
};
/* Decode existing monochrome font bits into native GS rectangle commands.
 * No destination pixels, CPU framebuffer, readback or software rasterizer.
 * All coordinates/clips are relative to the supplied owned color target.
 * Preflight the entire command plan before fencing/submitting anything.
 * One submission thread; restore the previous Mesa color/depth binding.
 */
int ps2_bitmap_draw(struct ps2_render_client *,uint32_t target,
    const struct ps2_bitmap_glyph *,unsigned count,
    const struct ps2_bitmap_box *,unsigned clip_count,int msb_first,
    uint32_t foreground,const struct ps2_bitmap_box *background,uint32_t background_color);
/* Preflight existing source bits, then emit clipped geometry in glyph order.
 * The consumer is a native colour GC span callback, never a CPU rasterizer. */
int ps2_bitmap_spans(const struct ps2_surface *,const struct ps2_bitmap_glyph *,unsigned count,
    const struct ps2_bitmap_box *,unsigned clip_count,int msb_first,const struct ps2_bitmap_box *background,
    int (*emit)(void *,unsigned x,unsigned y,unsigned width,unsigned height,int background),void *);
#endif
