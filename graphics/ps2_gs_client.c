/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_client.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(struct ps2gs_caps) == 80, "GS capability ABI");
_Static_assert(sizeof(struct ps2gs_submit) == 32, "GS submission ABI");
_Static_assert(sizeof(struct ps2gs_command) == 64, "GS command ABI");
_Static_assert(sizeof(struct ps2gs_upload) == 48, "GS image upload ABI");

static int fail(struct ps2_gs_client *gs, int error)
{
    if (!gs->error) gs->error = error;
    gs->count = 0;
    errno = gs->error;
    return -1;
}

static int request(struct ps2_gs_client *gs, unsigned long operation, void *data)
{
    for (unsigned retry = 0; retry < 100; ++retry) {
        if (!ioctl(gs->fd, operation, data)) return 0;
        /* EAGAIN is the driver's pre-submission console-lock contention.
         * Other errors may follow drawing, so do not retry them. */
        if (errno != EAGAIN) return fail(gs, errno);
        struct timespec delay = { .tv_nsec = 10000000 };
        while (nanosleep(&delay, &delay) < 0) {
            if (errno != EINTR) return fail(gs, errno);
        }
    }
    return fail(gs, EAGAIN);
}

int ps2_gs_open(struct ps2_gs_client *gs, const char *device)
{
    memset(gs, 0, sizeof(*gs));
    gs->fd = open(device, O_RDWR | O_CLOEXEC);
    if (gs->fd < 0) return fail(gs, errno);
    if (request(gs, PS2GS_GET_CAPS, &gs->caps)) goto error;
    if (gs->caps.abi != PS2GS_ABI_VERSION || !gs->caps.width ||
        !gs->caps.height || gs->caps.width > 2048 || gs->caps.height > 2048 ||
        (gs->caps.bits_per_pixel != 16 && gs->caps.bits_per_pixel != 32) ||
        gs->caps.max_commands != PS2GS_MAX_COMMANDS ||
        (gs->caps.features & (PS2GS_HW_FILL | PS2GS_HW_COPY | PS2GS_HW_TRIANGLE)) !=
            (PS2GS_HW_FILL | PS2GS_HW_COPY | PS2GS_HW_TRIANGLE)) {
        fail(gs, ENOTSUP);
        goto error;
    }
    return 0;
error:
    close(gs->fd); gs->fd = -1; errno = gs->error; return -1;
}

void ps2_gs_close(struct ps2_gs_client *gs)
{
    /* Closing a failed session discards its queue. It never draws implicitly. */
    if (gs->fd >= 0) close(gs->fd);
    gs->fd = -1; gs->count = 0;
}

int ps2_gs_flush(struct ps2_gs_client *gs)
{
    if (gs->error) return fail(gs, gs->error);
    if (gs->fd < 0) return fail(gs, EBADF);
    if (!gs->count) return 0;
    struct ps2gs_submit submit = { .abi = gs->caps.abi, .count = gs->count,
        .width = gs->caps.width, .height = gs->caps.height,
        .bits_per_pixel = gs->caps.bits_per_pixel,
        .commands = (uintptr_t)gs->commands };
    if (request(gs, PS2GS_SUBMIT, &submit)) return -1;
    gs->count = 0;
    return 0;
}

static int enqueue(struct ps2_gs_client *gs, struct ps2gs_command command)
{
    if (gs->error) return fail(gs, gs->error);
    if (gs->fd < 0) return fail(gs, EBADF);
    if (gs->count == PS2GS_MAX_COMMANDS && ps2_gs_flush(gs)) return -1;
    gs->commands[gs->count++] = command;
    return 0;
}

static int rectangle(struct ps2_gs_client *gs, unsigned x, unsigned y,
                     unsigned width, unsigned height)
{
    return width && height && x < gs->caps.width && y < gs->caps.height &&
        width <= gs->caps.width - x && height <= gs->caps.height - y;
}

int ps2_gs_fill(struct ps2_gs_client *gs, unsigned x, unsigned y,
               unsigned width, unsigned height, uint32_t rgba)
{
    if (!rectangle(gs, x, y, width, height)) return fail(gs, EINVAL);
    return enqueue(gs, (struct ps2gs_command) { .op = PS2GS_FILL,
        .width = width, .height = height,
        .v = {{ .x = x * 16, .y = y * 16, .rgba = rgba }} });
}

int ps2_gs_copy(struct ps2_gs_client *gs, unsigned sx, unsigned sy,
               unsigned dx, unsigned dy, unsigned width, unsigned height)
{
    if (!rectangle(gs, sx, sy, width, height) || !rectangle(gs, dx, dy, width, height))
        return fail(gs, EINVAL);
    if (width % (gs->caps.bits_per_pixel == 16 ? 4 : 2)) return fail(gs, ENOTSUP);
    return enqueue(gs, (struct ps2gs_command) { .op = PS2GS_COPY,
        .width = width, .height = height,
        .v = {{ .x = sx * 16, .y = sy * 16 }, { .x = dx * 16, .y = dy * 16 }} });
}

int ps2_gs_triangle(struct ps2_gs_client *gs, const struct ps2gs_vertex vertices[3], int depth)
{
    if (depth != 0 && depth != 1) return fail(gs, EINVAL);
    if (depth && !(gs->caps.features & PS2GS_HW_DEPTH16)) return fail(gs, ENOTSUP);
    for (unsigned i = 0; i < 3; ++i) {
        if (vertices[i].x > gs->caps.width * 16 || vertices[i].y > gs->caps.height * 16 ||
            vertices[i].z > 65535) return fail(gs, EINVAL);
    }
    struct ps2gs_command command = { .op = PS2GS_TRIANGLE,
        .flags = depth ? PS2GS_DEPTH_TEST : 0 };
    memcpy(command.v, vertices, sizeof(command.v));
    return enqueue(gs, command);
}

int ps2_gs_clear_depth(struct ps2_gs_client *gs)
{
    if (!(gs->caps.features & PS2GS_HW_DEPTH16)) return fail(gs, ENOTSUP);
    return enqueue(gs, (struct ps2gs_command) { .op = PS2GS_CLEAR_DEPTH });
}

int ps2_gs_upload(struct ps2_gs_client *gs, unsigned x, unsigned y,
                  unsigned width, unsigned height, const void *pixels,
                  size_t source_bytes, size_t stride)
{
    if (gs->error) return fail(gs, gs->error);
    if (gs->fd < 0) return fail(gs, EBADF);
    if (!(gs->caps.features & PS2GS_HW_UPLOAD)) return fail(gs, ENOTSUP);
    if (!pixels || !rectangle(gs, x, y, width, height)) return fail(gs, EINVAL);
    unsigned bpp = gs->caps.bits_per_pixel / 8;
    size_t row = (size_t)width * bpp;
    if (stride < row || (height > 1 && stride > (SIZE_MAX - row) / (height - 1)))
        return fail(gs, EINVAL);
    size_t required = (height - 1) * stride + row;
    if (source_bytes < required || (uintptr_t)pixels > UINTPTR_MAX - (required - 1))
        return fail(gs, EINVAL);
    if (ps2_gs_flush(gs)) return -1;
    unsigned char packed[PS2GS_MAX_UPLOAD_BYTES];
    /* Limit each ioctl, including wide rows. Padding from the source never
     * reaches the GS, and all final quadword padding is done by the kernel. */
    for (unsigned column = 0; column < width;) {
        unsigned w = width - column;
        if (w > PS2GS_MAX_UPLOAD_BYTES / bpp) w = PS2GS_MAX_UPLOAD_BYTES / bpp;
        size_t packed_row = (size_t)w * bpp;
        unsigned rows = PS2GS_MAX_UPLOAD_BYTES / packed_row;
        for (unsigned line = 0; line < height;) {
            unsigned h = height - line;
            if (h > rows) h = rows;
            for (unsigned i = 0; i < h; ++i)
                memcpy(packed + i * packed_row,
                    (const unsigned char *)pixels + (line + i) * stride + column * bpp,
                    packed_row);
            struct ps2gs_upload upload = { .abi = gs->caps.abi,
                .width = gs->caps.width, .height = gs->caps.height,
                .bits_per_pixel = gs->caps.bits_per_pixel,
                .x = x + column, .y = y + line, .image_width = w, .image_height = h,
                .bytes = h * packed_row, .pixels = (uintptr_t)packed };
            if (request(gs, PS2GS_UPLOAD, &upload)) return -1;
            line += h;
        }
        column += w;
    }
    return 0;
}

uint32_t ps2_gs_pixel_color(uint32_t pixel, unsigned bits_per_pixel)
{
    if (bits_per_pixel == 32) return 0x80000000 | (pixel & 0x00ffffff);
    /* GS CT16 is RGB555, red in bits 0..4. This is not RGB565. */
    unsigned r = ((pixel & 31) * 255 + 15) / 31;
    unsigned g = (((pixel >> 5) & 31) * 255 + 15) / 31;
    unsigned b = (((pixel >> 10) & 31) * 255 + 15) / 31;
    return 0x80000000 | r | (g << 8) | (b << 16);
}
