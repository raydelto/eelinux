/* SPDX-License-Identifier: GPL-2.0 */
/* Each existing typed packet uses at most 32 qwords. Eight joined packets
 * occupy at most 242 qwords including one tag and the final FINISH. Keep the
 * existing 256-qword DMA allocation and bound time spent under the IRQ lock. */
#define PS2GS_DMA_BATCH_COUNT 8
#define PS2GS_INTERNAL_BATCH (~0U)
struct ps2gs_draw_batch {
	u32 operation, count, mask, mode;
	const void *requests;
	const struct ps2gs_buffer *texture;
};
static size_t ps2gs_package_batch(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_buffer *depth,
		const struct ps2gs_draw_batch *batch)
{
	union package one[32], *p = base + 1;
	u32 i;
	for (i = 0; i < batch->count; ++i) {
		size_t n;
		if (batch->operation == PS2GS_SOLID_LOGIC)
			n = ps2gs_package_solid_logic(one, color,
				(const struct ps2gs_solid_logic *)batch->requests + i);
		else if (batch->operation == PS2GS_RENDER_SUBMIT)
			n = ps2gs_package_render(one, color, depth,
				(const struct ps2gs_command *)batch->requests + i);
		else if (batch->operation == PS2GS_TEXTURE_SUBMIT)
			n = ps2gs_package_texture(one, color, depth, batch->texture,
				(const struct ps2gs_texture_triangle *)batch->requests + i);
		else
			n = ps2gs_package_transfer(one, color, batch->texture,
				(const struct ps2gs_texture_triangle *)batch->requests + i,
				batch->mask, batch->mode);
		/* Omit each private GIF tag and FINISH; preserve every state and
		 * primitive word in order, then wait once for the combined packet. */
		memcpy(p, one + 1, (n - 2) * sizeof(*p));
		p += n - 2;
	}
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
