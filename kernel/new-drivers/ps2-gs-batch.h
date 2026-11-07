/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_BATCH_H
#define _UAPI_PS2_GS_BATCH_H
#include "ps2-gs-logic.h"
#define PS2GS_HW_SOLID_BATCH (1U << 11)
/* Bounded, ordered solid operations with one shared target. Every rectangle
 * is snapshotted and validated before drawing. Native RGB bits, masks and
 * Boolean operations retain SOLID_LOGIC semantics, including preserved alpha.
 * No pixel staging, raw packets, registers or physical addresses are exposed. */
struct ps2gs_solid_batch {
	__u32 abi, handle, count, plane_mask;
	__u32 alu, width, height, bits_per_pixel;
	__aligned_u64 rectangles;
};
#define PS2GS_SOLID_BATCH _IOW('G', 0x7e, struct ps2gs_solid_batch)
#endif
