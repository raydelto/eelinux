/* SPDX-License-Identifier: GPL-2.0 */
/* Pure GS page allocator. Caller serializes it and supplies conservative
 * front/font/depth reservations. Whole 8 KiB pages avoid CT16/CT32 swizzle
 * aliasing. Never reuse a handle in one open session, including failed creates.
 */
#define PS2GS_PAGE_BYTES 8192U
#define PS2GS_PAGE_COUNT 512U
struct ps2gs_buffer {
	u32 handle, width, height, buffer_width, bits_per_pixel;
	u32 base, bytes;
};
struct ps2gs_arena {
	u32 begin, end, next_handle;
	u8 used[PS2GS_PAGE_COUNT];
	struct ps2gs_buffer buffers[PS2GS_MAX_SURFACES];
};

static int ps2gs_arena_init(struct ps2gs_arena *arena, u32 begin, u32 end)
{
	if (begin > end || end > PS2GS_PAGE_COUNT * PS2GS_PAGE_BYTES ||
	    begin % PS2GS_PAGE_BYTES || end % PS2GS_PAGE_BYTES)
		return -EINVAL;
	memset(arena, 0, sizeof(*arena));
	arena->begin = begin / PS2GS_PAGE_BYTES;
	arena->end = end / PS2GS_PAGE_BYTES;
	arena->next_handle = 1;
	return 0;
}

static struct ps2gs_buffer *ps2gs_arena_lookup(struct ps2gs_arena *arena, u32 handle)
{
	u32 i;
	if (!handle)
		return NULL;
	for (i = 0; i < PS2GS_MAX_SURFACES; ++i)
		if (arena->buffers[i].handle == handle)
			return &arena->buffers[i];
	return NULL;
}

static int ps2gs_arena_create(struct ps2gs_arena *arena, u32 width, u32 height,
			    u32 bpp, struct ps2gs_buffer **result)
{
	u32 slot, stride, rows, pages, first, i;
	*result = NULL;
	if (!width || !height || width > 2048 || height > 2048 ||
	    (bpp != 16 && bpp != 32))
		return -EINVAL;
	if (!arena->next_handle)
		return -EOVERFLOW;
	stride = ALIGN(width, 64);
	rows = ALIGN(height, bpp == 32 ? 32 : 64);
	pages = stride * rows * (bpp / 8) / PS2GS_PAGE_BYTES;
	for (slot = 0; slot < PS2GS_MAX_SURFACES; ++slot)
		if (!arena->buffers[slot].handle)
			break;
	if (slot == PS2GS_MAX_SURFACES || pages > arena->end - arena->begin)
		return -ENOSPC;
	for (first = arena->begin; first <= arena->end - pages; ++first) {
		for (i = 0; i < pages && !arena->used[first + i]; ++i)
			;
		if (i != pages) {
			first += i;
			continue;
		}
		memset(arena->used + first, 1, pages);
		arena->buffers[slot] = (struct ps2gs_buffer) {
			.handle = arena->next_handle++, .width = width, .height = height,
			.buffer_width = stride, .bits_per_pixel = bpp,
			.base = first * PS2GS_PAGE_BYTES, .bytes = pages * PS2GS_PAGE_BYTES };
		*result = &arena->buffers[slot];
		return 0;
	}
	return -ENOSPC;
}

static int ps2gs_arena_destroy(struct ps2gs_arena *arena, u32 handle)
{
	struct ps2gs_buffer *buffer = ps2gs_arena_lookup(arena, handle);
	if (!buffer)
		return -ENOENT;
	memset(arena->used + buffer->base / PS2GS_PAGE_BYTES, 0,
	       buffer->bytes / PS2GS_PAGE_BYTES);
	memset(buffer, 0, sizeof(*buffer));
	return 0;
}

static bool ps2gs_buffer_rect(const struct ps2gs_buffer *buffer,
			     u32 x, u32 y, u32 width, u32 height)
{
	return width && height && x < buffer->width && y < buffer->height &&
		width <= buffer->width - x && height <= buffer->height - y;
}
