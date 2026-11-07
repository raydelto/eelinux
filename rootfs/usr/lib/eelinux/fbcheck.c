/* Check the Linux framebuffer contract needed by Xorg's fbdev driver.
 * No display memory is written and no video mode is changed.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *device = argc == 2 ? argv[1] : "/dev/fb0";
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    void *mapping;
    long page_size;
    size_t length;
    int fd;

    if (argc > 2) {
        fprintf(stderr, "Usage: fbcheck [/dev/fb0]\n");
        return 2;
    }
    fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", device, strerror(errno));
        return 1;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fixed) < 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &variable) < 0) {
        fprintf(stderr, "%s does not provide Linux framebuffer information: %s\n",
                device, strerror(errno));
        close(fd);
        return 1;
    }
    if (fixed.type != FB_TYPE_PACKED_PIXELS ||
        fixed.visual != FB_VISUAL_TRUECOLOR || variable.bits_per_pixel != 16 ||
        variable.nonstd != 0 || variable.grayscale != 0 ||
        fixed.smem_len == 0 || fixed.line_length == 0 ||
        variable.red.length != 5 || variable.green.length != 5 || variable.blue.length != 5 ||
        variable.green.offset != 5 ||
        !((variable.red.offset == 0 && variable.blue.offset == 10) ||
          (variable.red.offset == 10 && variable.blue.offset == 0)) ||
        !((variable.transp.length == 0) ||
          (variable.transp.length == 1 && variable.transp.offset == 15)) ||
        variable.red.msb_right || variable.green.msb_right || variable.blue.msb_right) {
        fprintf(stderr, "The PS2 desktop requires mapped RGB555 truecolor (depth 15, 16-bit storage).\n"
                "Reported channels: R=%u:%u G=%u:%u B=%u:%u A=%u:%u; storage=%u bpp.\n",
                variable.red.offset, variable.red.length, variable.green.offset, variable.green.length,
                variable.blue.offset, variable.blue.length, variable.transp.offset, variable.transp.length,
                variable.bits_per_pixel);
        close(fd);
        return 1;
    }
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "Cannot determine the memory page size.\n");
        close(fd);
        return 1;
    }
    length = fixed.smem_len < (unsigned long)page_size ? fixed.smem_len : (size_t)page_size;
    mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "The framebuffer cannot be mapped for Xorg fbdev: %s.\n"
                "The current PS2 GS driver needs a mapped scanout buffer and update support.\n",
                strerror(errno));
        close(fd);
        return 1;
    }
    munmap(mapping, length);
    close(fd);
    printf("Framebuffer interface passed: %ux%u, depth 15 in 16 bpp; R=%u:%u G=%u:%u B=%u:%u A=%u:%u.\n"
           "Hardware drawing remains unvalidated.\n", variable.xres, variable.yres,
           variable.red.offset, variable.red.length, variable.green.offset, variable.green.length,
           variable.blue.offset, variable.blue.length, variable.transp.offset, variable.transp.length);
    return 0;
}
