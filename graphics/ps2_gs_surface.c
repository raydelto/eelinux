/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_surface.h"
#include "ps2-gs-map-blit.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(struct ps2gs_surface_create) == 32, "surface create ABI");
_Static_assert(sizeof(struct ps2gs_surface_copy) == 48, "surface copy ABI");
_Static_assert(sizeof(struct ps2gs_surface_upload) == 40, "surface upload ABI");
_Static_assert(sizeof(struct ps2gs_surface_download) == 40, "surface download ABI");

static int fail(struct ps2_surface_client *gs, int error)
{
    if (!gs->error) gs->error = error;
    errno = gs->error;
    return -1;
}

int ps2_surface_front_access(struct ps2_surface_client *gs, uint32_t handle)
{
    if (gs->error) return fail(gs, gs->error);
    if (!handle && gs->front_access && gs->front_access(gs->front_access_data))
        return fail(gs, errno ? errno : EIO);
    return 0;
}
int ps2_surface_front_region(struct ps2_surface_client *gs,uint32_t handle,
    unsigned x,unsigned y,unsigned width,unsigned height)
{
    if(gs->error)return fail(gs,gs->error);
    if(handle || !width || !height)return 0;
    if(gs->front_region){
        if(gs->front_region(gs->front_access_data,x,y,width,height))
            return fail(gs,errno?errno:EIO);
        return 0;
    }
    return ps2_surface_front_access(gs,handle);
}

static int pause_retry(struct ps2_surface_client *gs)
{
    struct timespec delay = { .tv_nsec = 10000000 };
    while (nanosleep(&delay, &delay) < 0)
        if (errno != EINTR) return fail(gs, errno);
    return 0;
}

static int request(struct ps2_surface_client *gs, unsigned long op, void *data)
{
    if (gs->error) return fail(gs, gs->error);
    if (gs->fd < 0) return fail(gs, EBADF);
    for (unsigned retry = 0; retry < 100; ++retry) {
        if (!ioctl(gs->fd, op, data)) return 0;
        if (errno != EAGAIN) return fail(gs, errno);
        if (pause_retry(gs)) return -1;
    }
    return fail(gs, EAGAIN);
}

int ps2_surface_open(struct ps2_surface_client *gs, const char *device)
{
    memset(gs, 0, sizeof(*gs)); gs->fd = -1;
    for (unsigned retry = 0; retry < 100; ++retry) {
        gs->fd = open(device, O_RDWR | O_CLOEXEC);
        if (gs->fd >= 0) break;
        if (errno != EAGAIN) return fail(gs, errno);
        if (pause_retry(gs)) return -1;
    }
    if (gs->fd < 0) return fail(gs, EAGAIN);
    if (request(gs, PS2GS_GET_CAPS, &gs->caps)) goto error;
    if (gs->caps.abi != PS2GS_ABI_VERSION || !gs->caps.width || !gs->caps.height ||
        gs->caps.width > 2048 || gs->caps.height > 2048 ||
        (gs->caps.bits_per_pixel != 16 && gs->caps.bits_per_pixel != 32) ||
        !(gs->caps.features & PS2GS_HW_SURFACES)) {
        fail(gs, ENOTSUP); goto error;
    }
    gs->front = (struct ps2_surface) { .width = gs->caps.width,
        .height = gs->caps.height, .bits_per_pixel = gs->caps.bits_per_pixel };
    return 0;
error:
    close(gs->fd); gs->fd = -1; errno = gs->error; return -1;
}

void ps2_surface_close(struct ps2_surface_client *gs)
{
    if (gs->fd >= 0) close(gs->fd);
    gs->fd = -1;
    memset(gs->surfaces, 0, sizeof(gs->surfaces));
}

const struct ps2_surface *ps2_surface_lookup(const struct ps2_surface_client *gs, uint32_t handle)
{
    if (!handle) return &gs->front;
    for (unsigned i = 0; i < PS2GS_MAX_SURFACES; ++i)
        if (gs->surfaces[i].handle == handle) return &gs->surfaces[i];
    return NULL;
}
uint32_t ps2_surface_native_mask(const struct ps2_surface_client *gs,uint32_t handle)
{
    const struct ps2_surface *s=ps2_surface_lookup(gs,handle);
    if(!s)return 0;
    if(s->bits_per_pixel==16)return 0x7fff;
    return handle && s->pixel_depth>=30 && (gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)?s->pixel_depth==32?UINT32_MAX:0x3fffffff:0xffffff;
}
unsigned ps2_surface_depth(const struct ps2_surface *s)
{return s->bits_per_pixel==16?15:s->pixel_depth==30?30:s->pixel_depth==32?32:24;}
int ps2_surface_set_depth(struct ps2_surface_client *gs,uint32_t handle,unsigned depth)
{
    if(!handle){errno=EINVAL;return -1;}
    for(unsigned i=0;i<PS2GS_MAX_SURFACES;++i)if(gs->surfaces[i].handle==handle){
        struct ps2_surface *s=gs->surfaces+i;
        if(depth!=(s->bits_per_pixel==16?15:24) && ((depth!=30 && depth!=32) || s->bits_per_pixel!=32)){
            errno=EINVAL;return -1;}
        if(depth>=30 && !(gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)){errno=ENOTSUP;return -1;}
        s->pixel_depth=depth;return 0;
    }
    errno=ENOENT;return -1;
}

int ps2_surface_create(struct ps2_surface_client *gs, unsigned width, unsigned height,
                       unsigned bpp, uint32_t *handle)
{
    if (handle) *handle = 0;
    if (!handle || !width || !height || width > 2048 || height > 2048 ||
        (bpp != 16 && bpp != 32)) return fail(gs, EINVAL);
    if (gs->error) return fail(gs, gs->error);
    unsigned slot;
    for (slot = 0; slot < PS2GS_MAX_SURFACES && gs->surfaces[slot].handle; ++slot) {}
    /* Local creation capacity has no side effects or failed DMA to replay. */
    if (slot == PS2GS_MAX_SURFACES) { errno=ENOSPC; return -1; }
    struct ps2gs_surface_create c = { .abi = PS2GS_ABI_VERSION,
        .width = width, .height = height, .bits_per_pixel = bpp };
    if (request(gs, PS2GS_SURFACE_CREATE, &c)) {
        if (gs->error==ENOSPC) {
            /* Arena exhaustion is pre-effect. Distinguish it from a sticky
             * kernel packet failure with the side-effect-free rejection of
             * destroying display handle zero. A healthy session returns
             * ENOENT; a failed session returns its original error first.
             * Never retry creation or clear an ambiguous hardware failure. */
            struct ps2gs_surface_destroy probe={.abi=PS2GS_ABI_VERSION};
            gs->error=0;
            if (request(gs,PS2GS_SURFACE_DESTROY,&probe)) {
                if (gs->error==ENOENT) gs->error=0;
            } else fail(gs,EPROTO);
            errno=gs->error?gs->error:ENOSPC;
        }
        return -1;
    }
    unsigned bw = (width + 63) & ~63U, rows = (height + (bpp == 16 ? 63 : 31)) &
        ~(bpp == 16 ? 63U : 31U);
    if (!c.handle || ps2_surface_lookup(gs, c.handle) || c.abi != PS2GS_ABI_VERSION ||
        c.width != width || c.height != height || c.bits_per_pixel != bpp || c.reserved ||
        c.buffer_width != bw || c.allocated_bytes != bw * rows * (bpp / 8) ||
        c.allocated_bytes > 4U * 1024 * 1024)
        return fail(gs, EPROTO);
    gs->surfaces[slot] = (struct ps2_surface) { .handle = c.handle,
        .width = width, .height = height, .bits_per_pixel = bpp,
        .buffer_width = bw, .bytes = c.allocated_bytes,.pixel_depth=bpp==16?15:24 };
    *handle = c.handle;
    return 0;
}

int ps2_surface_destroy(struct ps2_surface_client *gs, uint32_t handle)
{
    if (!handle || !ps2_surface_lookup(gs, handle)) return fail(gs, EINVAL);
    struct ps2gs_surface_destroy destroy = { .abi = PS2GS_ABI_VERSION, .handle = handle };
    if (request(gs, PS2GS_SURFACE_DESTROY, &destroy)) return -1;
    for (unsigned i = 0; i < PS2GS_MAX_SURFACES; ++i)
        if (gs->surfaces[i].handle == handle) memset(&gs->surfaces[i], 0, sizeof(gs->surfaces[i]));
    return 0;
}

static int rect(const struct ps2_surface *buffer, unsigned x, unsigned y,
                unsigned width, unsigned height)
{
    return buffer && width && height && x < buffer->width && y < buffer->height &&
        width <= buffer->width - x && height <= buffer->height - y;
}

int ps2_surface_fill(struct ps2_surface_client *gs, uint32_t handle,
                     unsigned x, unsigned y, unsigned width, unsigned height, uint32_t rgba)
{
    if (!rect(ps2_surface_lookup(gs, handle), x, y, width, height)) return fail(gs, EINVAL);
    struct ps2gs_surface_fill fill = { .abi = PS2GS_ABI_VERSION, .handle = handle,
        .x = x, .y = y, .width = width, .height = height, .rgba = rgba };
    if (ps2_surface_front_region(gs, handle,x,y,width,height)) return -1;
    return request(gs, PS2GS_SURFACE_FILL, &fill);
}

int ps2_surface_copy(struct ps2_surface_client *gs, uint32_t source, uint32_t destination,
                     unsigned sx, unsigned sy, unsigned dx, unsigned dy,
                     unsigned width, unsigned height)
{
    const struct ps2_surface *s = ps2_surface_lookup(gs, source), *d = ps2_surface_lookup(gs, destination);
    if (!rect(s, sx, sy, width, height) || !rect(d, dx, dy, width, height)) return fail(gs, EINVAL);
    if (s->bits_per_pixel != d->bits_per_pixel || width % (s->bits_per_pixel == 16 ? 4 : 2))
        return fail(gs, ENOTSUP);
    struct ps2gs_surface_copy copy = { .abi = PS2GS_ABI_VERSION,
        .source = source, .destination = destination, .sx = sx, .sy = sy,
        .dx = dx, .dy = dy, .width = width, .height = height };
    if (ps2_surface_front_region(gs, source,sx,sy,width,height) ||
        ps2_surface_front_region(gs, destination,dx,dy,width,height)) return -1;
    return request(gs, PS2GS_SURFACE_COPY, &copy);
}

int ps2_surface_upload(struct ps2_surface_client *gs, uint32_t handle,
                       unsigned x, unsigned y, unsigned width, unsigned height,
                       const void *pixels, size_t source_bytes, size_t stride)
{
    if (gs->error) return fail(gs, gs->error);
    const struct ps2_surface *buffer = ps2_surface_lookup(gs, handle);
    if (!pixels || !rect(buffer, x, y, width, height)) return fail(gs, EINVAL);
    unsigned cpp = buffer->bits_per_pixel / 8;
    size_t row = (size_t)width * cpp;
    if (stride < row || (height > 1 && stride > (SIZE_MAX - row) / (height - 1))) return fail(gs, EINVAL);
    size_t required = (height - 1) * stride + row;
    if (source_bytes < required || (uintptr_t)pixels > UINTPTR_MAX - (required - 1)) return fail(gs, EINVAL);
    if (ps2_surface_front_region(gs, handle,x,y,width,height)) return -1;
    unsigned char packed[PS2GS_MAX_UPLOAD_BYTES];
    for (unsigned column = 0; column < width;) {
        unsigned w = width - column;
        if (w > PS2GS_MAX_UPLOAD_BYTES / cpp) w = PS2GS_MAX_UPLOAD_BYTES / cpp;
        size_t packed_row = (size_t)w * cpp;
        unsigned rows = PS2GS_MAX_UPLOAD_BYTES / packed_row;
        for (unsigned line = 0; line < height;) {
            unsigned h = height - line;
            if (h > rows) h = rows;
            for (unsigned i = 0; i < h; ++i)
                memcpy(packed + i * packed_row,
                    (const unsigned char *)pixels + (line + i) * stride + column * cpp, packed_row);
            struct ps2gs_surface_upload upload = { .abi = PS2GS_ABI_VERSION,
                .handle = handle, .x = x + column, .y = y + line,
                .width = w, .height = h, .bytes = h * packed_row, .pixels = (uintptr_t)packed };
            if (request(gs, PS2GS_SURFACE_UPLOAD, &upload)) return -1;
            line += h;
        }
        column += w;
    }
    return 0;
}
int ps2_surface_download(struct ps2_surface_client *gs,uint32_t handle,
    unsigned x,unsigned y,unsigned width,unsigned height,void *pixels,size_t bytes)
{
    if(gs->error)return fail(gs,gs->error);
    if(!(gs->caps.features&PS2GS_HW_READBACK))return fail(gs,ENOTSUP);
    const struct ps2_surface *s=ps2_surface_lookup(gs,handle);
    if(!pixels || !rect(s,x,y,width,height) || width%(s->bits_per_pixel==16?4:2) ||
        !bytes || bytes>PS2GS_MAX_READBACK_BYTES || bytes%128 || bytes!=(size_t)width*height*(s->bits_per_pixel/8) ||
        (uintptr_t)pixels>UINTPTR_MAX-(bytes-1))return fail(gs,EINVAL);
    if(ps2_surface_front_region(gs,handle,x,y,width,height))return -1;
    struct ps2gs_surface_download download={.abi=PS2GS_ABI_VERSION,.handle=handle,.x=x,.y=y,
        .width=width,.height=height,.bytes=bytes,.pixels=(uintptr_t)pixels};
    return request(gs,PS2GS_SURFACE_DOWNLOAD,&download);
}
