/* SPDX-License-Identifier: GPL-2.0 */
struct ps2gs_map_blit_operation {
	struct ps2gs_lut_operation lookup;
	u32 sx, sy, dx, dy;
};
static size_t ps2gs_package_map_blit(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_map_blit_operation *op)
{
	struct ps2gs_lut_operation header = op->lookup;
	union package *p;
	u32 i, columns = DIV_ROUND_UP((op->sx & 3) + op->lookup.width, 4);
	header.width += op->dx;
	header.height += op->dy;
	header.count = 0;
	ps2gs_package_map(base, color, &header);
	base[1].gif.packed.ad.data.frame_1.fbw = color->buffer_width / 64;
	base[3].gif.packed.ad.data.scissor_1.scax0 = op->dx;
	base[3].gif.packed.ad.data.scissor_1.scay0 = op->dy;
	p = base + 17;
	for (i = op->lookup.first; i < op->lookup.first + op->lookup.count; ++i) {
		u32 bx = (op->sx & ~3U) + i % columns * 4;
		u32 by = (op->sy & ~1U) + i / columns * 2;
		u32 x = max_t(u32, bx, op->sx), y = max_t(u32, by, op->sy);
		u32 w = min_t(u32, bx + 4, op->sx + op->lookup.width) - x;
		u32 h = min_t(u32, by + 2, op->sy + op->lookup.height) - y;
		u32 channel = op->lookup.channel;
		u32 u = x / 8 * 16 + (x & 3) +
			(((x >> 2) ^ channel ^ (y >> 1)) & 1) * 4 + (channel >> 1) * 8;
		u32 v = y / 8 * 16 + (y & 1) + (channel & 1) * 2 + (y & 6) * 2;
		u32 dx = op->dx + x - op->sx, dy = op->dy + y - op->sy;
		p = ps2gs_texture_raw(p, gs_addr_uv, (u64)(u * 16 + 8) | (u64)(v * 16 + 8) << 16);
		GIF_PACKAGE_AD(p) { .addr = gs_addr_xyz2, .data.xyz2 = { .x = dx * 16, .y = dy * 16 } };
		p = ps2gs_texture_raw(p, gs_addr_uv, (u64)((u + w) * 16 + 8) | (u64)((v + h) * 16 + 8) << 16);
		GIF_PACKAGE_AD(p) { .addr = gs_addr_xyz2, .data.xyz2 = { .x = (dx + w) * 16, .y = (dy + h) * 16 } };
	}
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = color->base / 8192, .fbw = color->buffer_width / 64, .psm = gs_psm_ct32 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode, .reg0 = gif_reg_ad,
		.nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
