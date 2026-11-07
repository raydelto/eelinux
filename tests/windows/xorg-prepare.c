/* Device-I/O model for the real launcher: no framebuffer or input is touched. */
#define open model_open
#define close model_close
#define ioctl model_ioctl
#define stat model_stat
#define main prepare_main
#include "../../desktop/ps2-xorg-prepare.c"
#undef main
#undef open
#undef close
#undef ioctl
#undef stat
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

static struct fb_var_screeninfo hardware;
static int writes, reads, refuse_format;

int ps2_keyboard_current(const char *path, struct ps2_keyboard_layout *layout)
{
    (void)path;
    memset(layout, 0, sizeof(*layout));
    snprintf(layout->xkb, sizeof(layout->xkb), "gb");
    snprintf(layout->model, sizeof(layout->model), "pc105");
    snprintf(layout->label, sizeof(layout->label), "Test UK");
    return 0;
}

int model_open(const char *path, int flags, ...)
{
    (void)flags;
    if (!strcmp(path, "/dev/fb0")) return 10;
    if (!strcmp(path, "/dev/input/event3")) return 11;
    errno = ENOENT; return -1;
}
int model_close(int fd) { assert(fd == 10 || fd == 11); return 0; }
int model_stat(const char *path, struct model_stat *result)
{
    assert(!strcmp(path, "/dev/ps2-gs"));
    memset(result, 0, sizeof(*result)); result->st_mode = S_IFCHR; return 0;
}
int model_ioctl(int fd, unsigned long request, ...)
{
    va_list arguments; va_start(arguments, request);
    void *value = va_arg(arguments, void *); va_end(arguments);
    if (fd == 11) {
        assert(request == EVIOCGBIT(EV_KEY, sizeof(unsigned long) *
               ((KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long)))));
        unsigned long *keys = value;
        const unsigned codes[] = {KEY_A, KEY_Z, KEY_ENTER, KEY_LEFTSHIFT};
        for (unsigned i = 0; i < sizeof(codes) / sizeof(*codes); ++i)
            keys[codes[i] / (8 * sizeof(*keys))] |= 1UL << (codes[i] % (8 * sizeof(*keys)));
        return 0;
    }
    assert(fd == 10);
    if (request == FBIOGET_VSCREENINFO) {
        ++reads; memcpy(value, &hardware, sizeof(hardware)); return 0;
    }
    if (request == FBIOGET_FSCREENINFO) {
        memset(value, 0, sizeof(struct fb_fix_screeninfo)); return 0;
    }
    assert(request == FBIOPUT_VSCREENINFO); ++writes;
    const struct fb_var_screeninfo *wanted = value;
    assert(wanted->xres == 720 && wanted->yres == 576 && wanted->pixclock == 37037);
    assert(wanted->left_margin == 10 && wanted->vmode == FB_VMODE_NONINTERLACED);
    assert(wanted->bits_per_pixel == 16 && wanted->activate == FB_ACTIVATE_NOW);
    if (!refuse_format) {
        hardware.bits_per_pixel = 16;
        hardware.red.offset = 0; hardware.red.length = 5;
        hardware.green.offset = 5; hardware.green.length = 5;
        hardware.blue.offset = 10; hardware.blue.length = 5;
    }
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    hardware.xres = 720; hardware.yres = 576; hardware.pixclock = 37037;
    hardware.left_margin = 10; hardware.vmode = FB_VMODE_NONINTERLACED;
    hardware.bits_per_pixel = 16;
    hardware.red.length = 5; hardware.green.offset = 5; hardware.green.length = 5;
    hardware.blue.offset = 10; hardware.blue.length = 5;
    int scenario = atoi(argv[1]);
    if (scenario) hardware.bits_per_pixel = 32;
    if (scenario == 3) { hardware.bits_per_pixel = 16; hardware.blue.offset = 11; }
    refuse_format = scenario == 2;
    char *arguments[] = {argv[0], argv[2], argv[3], NULL};
    int result = prepare_main(3, arguments);
    assert(writes == (scenario != 0));
    assert(reads == (scenario ? 2 : 1));
    assert(result == (scenario == 2));
    return 0;
}
