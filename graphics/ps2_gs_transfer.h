/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_TRANSFER_CLIENT_H
#define PS2_GS_TRANSFER_CLIENT_H
#include "ps2_gs_texture.h"
#include "ps2-gs-transfer.h"
#include "ps2-gs-rgba-transfer.h"
int ps2_texture_transfer(struct ps2_render_client *, uint32_t texture,
    const struct ps2gs_texture_triangle *, unsigned count, uint32_t mask, unsigned mode);
int ps2_texture_rgba_transfer(struct ps2_render_client *,uint32_t texture,
    const struct ps2gs_texture_triangle *,unsigned count,uint32_t mask,unsigned mode);
int ps2_surface_rgba_rectangle(struct ps2_surface_client *,uint32_t source,uint32_t destination,
    unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width,unsigned height,uint32_t mask,unsigned mode);
#endif
