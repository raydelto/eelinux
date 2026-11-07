/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_LUT_CLIENT_H
#define PS2_GS_LUT_CLIENT_H
#include "ps2_gs_surface.h"
#include "ps2-gs-lut.h"
#include "ps2-gs-map.h"
#include "ps2-gs-map-blit.h"
/* Palette bytes are constant per operation; source/destination pixels stay
 * on GS. source and destination are owned 64x32 CT32 scratch surfaces. */
int ps2_surface_channel_lut(struct ps2_surface_client *, uint32_t source,
    uint32_t destination, uint32_t palette, unsigned width, unsigned height, uint32_t mask);
int ps2_surface_lut_palette(struct ps2_surface_client *, uint32_t palette, const unsigned char table[256]);
int ps2_surface_map_palette(struct ps2_surface_client *, uint32_t palette, const uint32_t table[256]);
int ps2_surface_channel_map(struct ps2_surface_client *, uint32_t source,
    uint32_t destination, uint32_t palette, unsigned width, unsigned height,
    uint32_t mask, unsigned source_channel);
int ps2_surface_channel_map_blit(struct ps2_surface_client *, uint32_t source,
    uint32_t destination,uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,
    unsigned width,unsigned height,uint32_t mask,unsigned source_channel);
int ps2_surface_channel_map_region(struct ps2_surface_client *, uint32_t source,
    uint32_t destination,uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,
    unsigned width,unsigned height,uint32_t mask,unsigned source_channel);
/* Extract coverage only: GS clears all scratch bytes and maps the alpha byte
 * into its low byte before readback. Source colour bytes never reach the CPU. */
int ps2_surface_alpha_read(struct ps2_surface_client *,uint32_t source,
    unsigned x,unsigned y,unsigned width,unsigned height,unsigned char *alpha,size_t bytes);
/* The picture's alpha offset selects byte 0 (BGRA) or byte 3 (ARGB).
 * Clear the scratch first so no other source byte reaches the CPU. */
int ps2_surface_alpha_read_byte(struct ps2_surface_client *,uint32_t source,unsigned alpha_byte,
    unsigned x,unsigned y,unsigned width,unsigned height,unsigned char *alpha,size_t bytes);
/* Decode BGRA/BGRX bytes {alpha,R,G,B} into native RGB, entirely on GS. */
int ps2_surface_rgb_rotate(struct ps2_surface_client *,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width,unsigned height);
/* Map a red-byte Boolean/coverage result into alpha. The optional typed RGBA
 * transfer uses a separate scratch page; the legacy kernel retains its map. */
int ps2_surface_alpha_write(struct ps2_surface_client *,uint32_t red_source,uint32_t rgba_scratch,
    uint32_t destination,uint32_t palette,unsigned dx,unsigned dy,unsigned width,unsigned height,uint32_t mask);
#endif
