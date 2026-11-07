/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_SURFACE_CLIENT_H
#define PS2_GS_SURFACE_CLIENT_H
#include <stdint.h>
#include <stddef.h>
#include "ps2-gs-surface.h"
#include "ps2-gs-readback.h"

struct ps2_surface {
    uint32_t handle, width, height, bits_per_pixel, buffer_width, bytes, pixel_depth;
};
/* One drawing thread per session. Xorg and an in-process Mesa context share
 * this object; a second process cannot open the exclusive device. Metadata
 * describes GS storage, never a CPU mapping or a user-selected VRAM address. */
struct ps2_surface_client {
    int fd, error;
    struct ps2gs_caps caps;
    struct ps2_surface front, surfaces[PS2GS_MAX_SURFACES];
    /* One optional screen-owned observer, called before native front reads or
     * writes. It must not flush the pending render queue; save-under cursors
     * restore through synchronous GS draws and suppress their own recursion. */
    int (*front_access)(void *);
    void *front_access_data;
    int (*front_region)(void *, unsigned, unsigned, unsigned, unsigned);
};
int ps2_surface_front_access(struct ps2_surface_client *, uint32_t handle);
int ps2_surface_front_region(struct ps2_surface_client *, uint32_t handle,
    unsigned x, unsigned y, unsigned width, unsigned height);
int ps2_surface_open(struct ps2_surface_client *, const char *device);
void ps2_surface_close(struct ps2_surface_client *);
const struct ps2_surface *ps2_surface_lookup(const struct ps2_surface_client *, uint32_t handle);
uint32_t ps2_surface_native_mask(const struct ps2_surface_client *,uint32_t handle);
unsigned ps2_surface_depth(const struct ps2_surface *);
int ps2_surface_set_depth(struct ps2_surface_client *,uint32_t handle,unsigned depth);
/* Healthy creation-capacity rejection returns ENOSPC without poisoning the
 * session. Hardware, protocol and ambiguous kernel failures remain sticky. */
int ps2_surface_create(struct ps2_surface_client *, unsigned width, unsigned height,
                       unsigned bpp, uint32_t *handle);
int ps2_surface_destroy(struct ps2_surface_client *, uint32_t handle);
int ps2_surface_fill(struct ps2_surface_client *, uint32_t handle,
                     unsigned x, unsigned y, unsigned width, unsigned height, uint32_t rgba);
int ps2_surface_copy(struct ps2_surface_client *, uint32_t source, uint32_t destination,
                     unsigned sx, unsigned sy, unsigned dx, unsigned dy,
                     unsigned width, unsigned height);
int ps2_surface_upload(struct ps2_surface_client *, uint32_t handle,
                       unsigned x, unsigned y, unsigned width, unsigned height,
                       const void *pixels, size_t source_bytes, size_t stride);
int ps2_surface_download(struct ps2_surface_client *, uint32_t handle,
    unsigned x,unsigned y,unsigned width,unsigned height,void *pixels,size_t bytes);
#endif
