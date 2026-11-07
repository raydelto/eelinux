/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_RENDER_H
#define _UAPI_PS2_GS_RENDER_H
#include "ps2-gs-surface.h"

#define PS2GS_HW_OWNED_RENDER (1U << 6)
/* Depth objects use the same handle namespace, ownership and destroy ioctl.
 * color_bits_per_pixel selects Z16 for CT16 or Z16S for CT32. They cannot
 * be filled/copied/uploaded as color. Storage is zeroed before publication.
 */
struct ps2gs_depth_create {
	__u32 abi, width, height, color_bits_per_pixel;
	__u32 handle, buffer_width, allocated_bytes, reserved;
};
/* Snapshot and validate the entire batch before drawing. target=0 is front;
 * depth=0 disables depth storage. All selected buffers must belong to this
 * file. Depth dimensions and padded width must match the color target.
 * Commands are existing ABI-1 FILL/TRIANGLE/CLEAR_DEPTH, not COPY. No blending,
 * textures or arbitrary depth functions are implied. Larger Z is nearer;
 * tested triangles use GEQUAL and update Z. CLEAR_DEPTH writes zero.
 */
struct ps2gs_render_submit {
	__u32 abi, target, depth, count;
	__u32 width, height, bits_per_pixel, reserved;
	__aligned_u64 commands;
};
#define PS2GS_DEPTH_CREATE _IOWR('G', 0x78, struct ps2gs_depth_create)
#define PS2GS_RENDER_SUBMIT _IOW('G', 0x79, struct ps2gs_render_submit)
#endif
