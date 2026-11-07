/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_READBACK_H
#define PS2_XORG_READBACK_H
#include "ps2_context_bridge.h"
RegionPtr ps2_readback_bitmap_region(struct pipe_context *,PixmapPtr);
void ps2_readback_image(struct pipe_context *,DrawablePtr,int x,int y,int width,int height,
    unsigned format,unsigned long plane_mask,char *output);
void ps2_readback_spans(struct pipe_context *,DrawablePtr,int max_width,DDXPointPtr,int *,int count,char *output);
#endif
