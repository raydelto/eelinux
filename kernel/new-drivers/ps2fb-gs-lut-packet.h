/* SPDX-License-Identifier: GPL-2.0 */
/* CT32 and T8 share block ordering. Within a block, CT32's byte address
 * bits are {channel[1:0],x0,y0,x1,x2,y1,y2}. T8 rearranges these as
 * {v1,u3,u0,v0,u1,u2 XOR v1 XOR v2,v2,v3}. This mapping is derived from
 * Sony's 2001-2004 "32bit Colour Channel Shifting" column diagrams.
 * Four-wide, two-high strips are contiguous in both layouts. */
struct ps2gs_lut_operation {
	const struct ps2gs_buffer *texture, *palette;
	u32 width, height, mask, channel, first, count;
};
static size_t ps2gs_package_lut(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_lut_operation *op)
{
	union package *p = base + 1;
	u32 i, columns = DIV_ROUND_UP(op->width, 4);
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = color->base / 8192, .fbw = 1,
			.psm = gs_psm_ct32, .fbmsk = ~op->mask } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyoffset_2 };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_scissor_2,
		.data.scissor_1 = { .scax1 = op->width - 1, .scay1 = op->height - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_zbuf_2,
		.data.zbuf = { .psm = 0xa, .zmsk = gs_zbuf_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_test_2,
		.data.test_1 = { .zte = gs_depth_test_on, .ztst = gs_depth_pass } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_texflush };
	/* T8 reinterprets precisely the source's one 8192-byte CT32 page.
	 * CSM1 needs a physical 16x16 palette and swaps index bits 3 and 4. */
	p = ps2gs_texture_raw(p, gs_addr_tex0_2,
		(u64)(op->texture->base / 256) | (2ULL << 14) | (0x13ULL << 20) |
		(7ULL << 26) | (6ULL << 30) | (1ULL << 35) |
		((u64)(op->palette->base / 256) << 37) | (1ULL << 61));
	GIF_PACKAGE_AD(p) { .addr = gs_addr_tex1_2,
		.data.tex1 = { .lcm = gs_lcm_fixed, .mmin = gs_lod_nearest, .mmag = gs_lod_nearest } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_clamp_2,
		.data.clamp_2 = { .wms = gs_clamp_region_clamp, .wmt = gs_clamp_region_clamp,
			.maxu = 127, .maxv = 63 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prmodecont, .data.prmodecont = { .ac = 1 } };
	p = ps2gs_texture_raw(p, gs_addr_colclamp, 1);
	GIF_PACKAGE_AD(p) { .addr = gs_addr_dthe };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_pabe };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_fba_2 };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prim,
		.data.prim = { .prim = gs_sprite, .ctxt = gs_context_2,
			.tme = gs_texturing_on, .fst = gs_texturing_uv } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_rgbaq,
		.data.rgbaq = { .r = 128, .g = 128, .b = 128, .a = 128, .q = 0x3f800000 } };
	for (i = op->first; i < op->first + op->count; ++i) {
		u32 x = i % columns * 4, y = i / columns * 2;
		u32 w = min_t(u32, 4, op->width - x), h = min_t(u32, 2, op->height - y);
		u32 u = x / 8 * 16 + (x & 3) +
			(((x >> 2) ^ op->channel ^ (y >> 1)) & 1) * 4 + (op->channel >> 1) * 8;
		u32 v = y / 8 * 16 + (y & 1) + (op->channel & 1) * 2 + (y & 6) * 2;
		p = ps2gs_texture_raw(p, gs_addr_uv, (u64)(u * 16 + 8) | (u64)(v * 16 + 8) << 16);
		GIF_PACKAGE_AD(p) { .addr = gs_addr_xyz2, .data.xyz2 = { .x = x * 16, .y = y * 16 } };
		p = ps2gs_texture_raw(p, gs_addr_uv, (u64)((u + w) * 16 + 8) | (u64)((v + h) * 16 + 8) << 16);
		GIF_PACKAGE_AD(p) { .addr = gs_addr_xyz2, .data.xyz2 = { .x = (x + w) * 16, .y = (y + h) * 16 } };
	}
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = color->base / 8192, .fbw = 1, .psm = gs_psm_ct32 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode, .reg0 = gif_reg_ad,
		.nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
