/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_MAP_BLIT_H
#define _UAPI_PS2_GS_MAP_BLIT_H
#include "ps2-gs-map.h"
#define PS2GS_HW_CHANNEL_MAP_BLIT (1U << 14)
/* Map one byte from an owned CT32 source rectangle into selected bits of a
 * distinct owned CT32 destination. The source rectangle fits in one 64x32
 * GS page. Palette is a third owned 16x16 CT32 surface. Coordinates and all
 * storage bounds are validated; no addresses/registers are user supplied. */
struct ps2gs_channel_map_blit {
	__u32 abi, source, destination, palette;
	__u32 width, height, plane_mask, source_channel;
	__u32 sx, sy, dx, dy;
};
#define PS2GS_CHANNEL_MAP_BLIT _IOW('G', 0x81, struct ps2gs_channel_map_blit)
#endif
