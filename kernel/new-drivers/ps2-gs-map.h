/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_MAP_H
#define _UAPI_PS2_GS_MAP_H
#include "ps2-gs-lut.h"
#define PS2GS_HW_CHANNEL_MAP (1U << 13)
/* Map one selected source byte through an owned RGBA palette. All selected
 * destination bits, including alpha, are replaced; other bits are preserved.
 * Three distinct owned surfaces: source/destination 64x32 CT32 and palette
 * 16x16 CT32. No user addresses or registers. Rectangles start at the origin. */
struct ps2gs_channel_map {
	__u32 abi, source, destination, palette;
	__u32 width, height, plane_mask, source_channel;
};
#define PS2GS_CHANNEL_MAP _IOW('G', 0x80, struct ps2gs_channel_map)
#endif
