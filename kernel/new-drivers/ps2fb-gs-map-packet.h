/* SPDX-License-Identifier: GPL-2.0 */
/* The indexed swizzle already addresses all four CT32 bytes. Enable RGBA
 * DECAL so a palette entry can also replace alpha with its full eight bits.
 * Keep the original RGB-only lookup packet and ABI unchanged. */
static size_t ps2gs_package_map(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_lut_operation *op)
{
	size_t i, count = ps2gs_package_lut(base, color, op);
	for (i = 1; i < count; ++i)
		if (base[i].gif.doubleword[1] == gs_addr_tex0_2)
			base[i].gif.doubleword[0] |= 1ULL << 34;
	return count;
}
