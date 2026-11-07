/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_TEXTURE_CLIENT_H
#define PS2_GS_TEXTURE_CLIENT_H
#include "ps2_gs_render.h"
struct ps2gs_texture_triangle;
int ps2_texture_front_region(struct ps2_render_client *,const struct ps2gs_texture_triangle *,unsigned);
#include "ps2-gs-texture.h"
/* Synchronous sampling on the same exclusive file. Fences queued rendering
 * first. No raw GS addresses, CPU rasterization or retries after effects. */
int ps2_texture_triangles(struct ps2_render_client *, uint32_t texture,
                         const struct ps2gs_texture_triangle *, unsigned count);
#endif
