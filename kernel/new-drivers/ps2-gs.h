/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Experimental bounded rendering ABI for the PS2 GS framebuffer.
 * No raw GS register writes, DMA addresses or userspace MMIO are accepted.
 * Rasterization always takes place in GS local memory. There is no CPU renderer.
 */
#ifndef _UAPI_PS2_GS_H
#define _UAPI_PS2_GS_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define PS2GS_ABI_VERSION 1
#define PS2GS_MAX_COMMANDS 64
#define PS2GS_HW_FILL       (1U << 0)
#define PS2GS_HW_COPY       (1U << 1)
#define PS2GS_HW_TRIANGLE   (1U << 2)
#define PS2GS_HW_DEPTH16    (1U << 3)
#define PS2GS_HW_UPLOAD     (1U << 4)
#define PS2GS_MAX_UPLOAD_BYTES 3072
#define PS2GS_DEPTH_TEST    (1U << 0)

enum ps2gs_operation {
	PS2GS_FILL = 1, PS2GS_COPY, PS2GS_TRIANGLE, PS2GS_CLEAR_DEPTH
};

/* x/y are unsigned 12.4 window coordinates, z is a 16-bit depth value.
 * Larger depth values are nearer. rgba is R | G<<8 | B<<16 | A<<24;
 * GS opaque alpha is 128. Blending is not exposed by this ABI.
 */
struct ps2gs_vertex { __u32 x, y, z, rgba; };

/* FILL: v[0] origin/color, width/height in pixels.
 * COPY: v[0] source, v[1] destination, width/height in pixels.
 * TRIANGLE: three vertices; width/height must be zero.
 * CLEAR_DEPTH: all fields except op must be zero; clears depth to zero.
 * All unused vertices/fields must be zero. Unsupported flags are errors.
 */
struct ps2gs_command {
	__u32 op, flags;
	struct ps2gs_vertex v[3];
	__u32 width, height;
};

struct ps2gs_caps {
	__u32 abi, features, width, height;
	__u32 bits_per_pixel, max_commands, depth_base, depth_bytes;
	__aligned_u64 completed_batches, fills, copies, triangles;
	__aligned_u64 depth_clears, timeouts;
};

struct ps2gs_submit {
	__u32 abi, count, width, height;
	__u32 bits_per_pixel, reserved;
	__aligned_u64 commands;
};

/* Packed native CT16/CT32 pixels, left to right, top to bottom. No row padding.
 * The source is snapshotted before the first GS side effect. The destination
 * is always the current framebuffer; users cannot select a VRAM address.
 * CT16 is red-low RGB555/A1, CT32 is red-low RGBA8 (opaque GS alpha = 128).
 * The final GIF quadword is zero padded by the kernel, never read from users.
 * Mode identity and the feature bit allow old ABI-1 clients/kernels to coexist.
 */
struct ps2gs_upload {
	__u32 abi, width, height, bits_per_pixel;
	__u32 x, y, image_width, image_height;
	__u32 bytes, reserved;
	__aligned_u64 pixels;
};

#define PS2GS_GET_CAPS _IOR('G', 0x70, struct ps2gs_caps)
#define PS2GS_SUBMIT   _IOW('G', 0x71, struct ps2gs_submit)
#define PS2GS_UPLOAD   _IOW('G', 0x72, struct ps2gs_upload)
#endif
