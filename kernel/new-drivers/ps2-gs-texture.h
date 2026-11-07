/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_TEXTURE_H
#define _UAPI_PS2_GS_TEXTURE_H
#include "ps2-gs-render.h"
#define PS2GS_HW_TEXTURE (1U << 7)
#define PS2GS_TEXTURE_MODULATE (1U << 1)
/* STQ are positive IEEE-754 binary32 bit patterns, not kernel floats.
 * S/Q and T/Q are normalized to TEX0's rounded power-of-two size, in [0,1];
 * Q is bounded to [2^-12,2^12]. Logical region clamp bounds NPOT sampling.
 * RGB level-zero CT16/CT32, nearest sampling, logical region clamp.
 * RGB replace or modulate; no texture alpha, blending, mipmaps or shaders.
 */
struct ps2gs_texture_vertex {
	struct ps2gs_vertex position;
	__u32 s, t, q;
};
struct ps2gs_texture_triangle {
	__u32 flags, reserved[3];
	struct ps2gs_texture_vertex v[3];
};
struct ps2gs_texture_submit {
	__u32 abi, target, depth, texture;
	__u32 count, width, height, bits_per_pixel;
	__aligned_u64 triangles;
};
/* texture must be a nonzero color handle owned by this file and must differ
 * from target. Texture dimensions are <=1024. All triangles are snapshotted
 * and checked before drawing; each packet invalidates the GS texture cache.
 */
#define PS2GS_TEXTURE_SUBMIT _IOW('G', 0x7a, struct ps2gs_texture_submit)
#endif
