/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_FORMAT_H
#define PS2_GS_FORMAT_H
#include "ps2_rop.h"
#include "ps2_composite.h"
enum ps2_byte_format {
    PS2_BYTE_RGB332,PS2_BYTE_BGR233,PS2_BYTE_ARGB2222,PS2_BYTE_ABGR2222,
    PS2_WORD_RGB565,PS2_WORD_BGR565,PS2_WORD_ARGB1555,PS2_WORD_ABGR1555,
    PS2_WORD_ARGB4444,PS2_WORD_ABGR4444,PS2_WORD_RGB555,PS2_WORD_BGR555,
    PS2_WORD_RGB444,PS2_WORD_BGR444
};
struct ps2_format_surface {
    struct ps2_surface_client *gs;
    struct ps2_surface original;
    uint32_t surface, words, scratch, palette;
    unsigned swap;
    uint32_t packed, rgba;
    struct ps2_picture_bitmap original_bytes;
    unsigned byte_format;
};
/* RGB444 pictures retain core X's native 15-bit storage. GS reconstructs
 * raw words and expands the nibbles; the CPU supplies constant palettes only. */
int ps2_format_decode444(struct ps2_format_surface *,struct ps2_surface_client *,uint32_t,unsigned swap);
int ps2_format_encode444(struct ps2_format_surface *);
/* Native depth-15 storage to/from packed CT32, entirely on GS. These views
 * share the scanline alias compositor with owned byte/word pixmaps. */
int ps2_format_decode_native15(struct ps2_format_surface *,struct ps2_surface_client *,uint32_t,unsigned format);
int ps2_format_store_native15(struct ps2_format_surface *);
int ps2_format_decode8(struct ps2_format_surface *,struct ps2_surface_client *,
    const struct ps2_picture_bitmap *,unsigned format);
int ps2_format_encode8(struct ps2_format_surface *,struct pipe_context *,unsigned char *,size_t,size_t);
int ps2_format_decode16(struct ps2_format_surface *,struct ps2_surface_client *,
    const struct ps2_picture_bitmap *,unsigned format);
int ps2_format_encode16(struct ps2_format_surface *,struct pipe_context *,unsigned char *,size_t,size_t);
int ps2_format_release(struct ps2_format_surface *);
/* GS-only packed-byte synchronization. No client transfer or pixel readback. */
int ps2_format_pack_region(struct ps2_format_surface *,unsigned x,unsigned y,unsigned width);
int ps2_format_refresh_row(struct ps2_format_surface *,unsigned y);
struct ps2_coverage_surface;
struct ps2_format_alias {
    struct ps2_format_surface *format[2];
    struct ps2_coverage_surface *coverage[2];
};
int ps2_format_alias_valid(const struct ps2_format_alias *,struct ps2_surface_client *,uint32_t parent,uint32_t map);
int ps2_format_alias_parent(struct ps2_format_alias *,unsigned x,unsigned y,unsigned width);
int ps2_format_alias_map(struct ps2_format_alias *,unsigned x,unsigned y,unsigned width,unsigned parent_y);
#endif
