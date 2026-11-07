/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_ROP_H
#define PS2_PIPE_ROP_H
#include "ps2_copy_batch.h"
#include "ps2_gs_transfer.h"
/* 56 KiB of GS VRAM, or 80 KiB with the optional raw32 alpha planes/palette:
 * two native snapshots and three CT32 bit planes.
 * No CPU pixel storage. Reserve once before fencing the original queue. */
struct ps2_rop_workspace {
    struct ps2_surface_client *gs;
    uint32_t source, destination, a, b, c;
    uint32_t alpha_source,alpha_destination,palette;
};
int ps2_rop_reserve(struct ps2_rop_workspace *, struct ps2_surface_client *, unsigned bpp);
int ps2_rop_release(struct ps2_rop_workspace *);
int ps2_rop_alpha_reserve(struct ps2_rop_workspace *);
int ps2_rop_sample(struct ps2_surface_client *, uint32_t source, uint32_t destination,
    unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned width, unsigned height,
    uint32_t mask, unsigned mode, unsigned factor);
int ps2_rop_capture(struct ps2_rop_workspace *, uint32_t source, uint32_t snapshot,
    unsigned sx, unsigned sy, unsigned width, unsigned height, unsigned *offset);
int ps2_rop_rectangle(struct ps2_rop_workspace *, uint32_t source, uint32_t destination,
    unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned width, unsigned height,
    uint32_t plane_mask, unsigned alu);
/* Read-only client transfer. Any plane masking is executed on GS first;
 * the CPU only packs native rows into the client's output buffer. */
int ps2_pipe_download(struct pipe_context *,uint32_t source,unsigned x,unsigned y,
    unsigned width,unsigned height,uint32_t plane_mask,void *pixels,size_t bytes,size_t stride);
int ps2_pipe_rop_copy_boxes(struct pipe_context *, uint32_t source, uint32_t destination,
    const struct ps2_pipe_copy_box *, unsigned count, uint32_t plane_mask, unsigned alu);
struct ps2_pipe_solid_box;
int ps2_pipe_tile_logic_geometry(struct pipe_context *, uint32_t source, uint32_t destination,
    const struct ps2_pipe_solid_box *, unsigned count, int32_t origin_x, int32_t origin_y,
    uint32_t plane_mask, unsigned alu);
#endif
