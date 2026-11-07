/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_COVERAGE_H
#define PS2_PIPE_COVERAGE_H
#include "ps2_composite.h"
struct ps2_coverage_surface {
    struct ps2_picture_bitmap original_bytes;
    struct ps2_surface_client *gs;
    uint32_t surface, packed, palette, bytes, rgba;
    unsigned width, height, bpp, msb_first, row;
    unsigned core, alpha_bits;
};
/* Original client bytes are uploaded without CPU pixel conversion. GS
 * channel lookups decode and encode A1/A8. Readback only serializes already
 * rendered packed bytes for Xorg's owned pixmap storage. */
int ps2_coverage_decode(struct ps2_coverage_surface *,struct ps2_surface_client *,
    const struct ps2_picture_bitmap *);
/* Core X11 stores unscaled A1 bits or A8 bytes in GS red for Boolean ALUs. */
int ps2_coverage_decode_core(struct ps2_coverage_surface *,struct ps2_surface_client *,
    const struct ps2_picture_bitmap *);
int ps2_coverage_encode(struct ps2_coverage_surface *,struct pipe_context *,
    unsigned char *pixels,size_t bytes,size_t stride);
int ps2_coverage_release(struct ps2_coverage_surface *);
int ps2_coverage_pack_region(struct ps2_coverage_surface *,unsigned x,unsigned y,unsigned width);
int ps2_coverage_refresh_row(struct ps2_coverage_surface *,unsigned y);
#endif
