/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "util/u_inlines.h"
#undef ARRAY_SIZE
#include "ps2_copy_window.h"
#include "ps2_blit.h"
#include "windowstr.h"
#include "regionstr.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Actual native screen CopyWindow and Mesa resource/transfer operations.
 * Minimal screen bookkeeping; no X server, Damage subsystem or TWM. */
BoxRec RegionEmptyBox = {0};
RegDataRec RegionEmptyData = {0}, RegionBrokenData = {0};
static unsigned private_slots;
static PixmapPtr front, backing;
static volatile sig_atomic_t stopped;
static jmp_buf abort_callback;
static int callback_failure;
Bool dixRegisterPrivateKey(DevPrivateKey key, DevPrivateType type, unsigned size)
{
    if (size || type != PRIVATE_SCREEN) return FALSE;
    if (!key->initialized) {
        if (private_slots == 16) return FALSE;
        *key = (DevPrivateKeyRec){.initialized = TRUE, .type = type, .offset = private_slots++*sizeof(void*)};
    }
    return TRUE;
}
void *exaGetPixmapDriverPrivate(PixmapPtr p) { return p->devPrivates; }
void exaMarkSync(ScreenPtr s) { (void)s; }
void FatalError(const char *format, ...)
{
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    callback_failure = 1; longjmp(abort_callback, 1);
}
static PixmapPtr get_front(ScreenPtr s) { (void)s; return front; }
static PixmapPtr get_window(WindowPtr w) { (void)w; return backing; }
static void lower_copy(WindowPtr w, DDXPointRec old, RegionPtr source)
{ (void)w; (void)old; (void)source; FatalError("ps2-gs: unexpected lower window drawing\n"); }
static Bool lower_close(ScreenPtr s) { return s->CopyWindow == lower_copy; }
static void interrupt(int sig) { (void)sig; stopped = 1; }
static int pause_scene(unsigned seconds)
{
    struct timespec delay = {.tv_sec = seconds};
    while (!stopped && nanosleep(&delay, &delay)) if (errno != EINTR) return -1;
    return stopped ? -1 : 0;
}
static int present(struct pipe_context *ctx, uint32_t handle, int sx, int sy,
    int dx, int dy, int width, int height)
{
    const struct ps2_pipe_blit_rect box = {.sx = sx, .sy = sy, .sw = width, .sh = height,
        .dx = dx, .dy = dy, .dw = width, .dh = height};
    return ps2_pipe_blit_rect(ctx, handle, 0, &box);
}
static int move(WindowPtr w, DDXPointRec old, RegionPtr source)
{
    if (setjmp(abort_callback)) return -1;
    w->drawable.pScreen->CopyWindow(w, old, source);
    return 0;
}
int main(int argc, char **argv)
{
    unsigned seconds = 3;
    if (argc == 2 && !strcmp(argv[1], "--brief")) seconds = 1;
    else if (argc != 1) { fputs("usage: ps2-gs-window-test [--brief]\n", stderr); return 2; }
    stopped = 0; callback_failure = 0;
    struct sigaction action = {.sa_handler = interrupt}; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL) || sigaction(SIGHUP, &action, NULL)) return 1;
    int tty = -1, fb = -1, graphics = 0, status = 1, mode;
    struct pipe_screen *screen = NULL; struct pipe_context *ctx = NULL;
    struct pipe_resource *source = NULL, *canvas = NULL; struct pipe_surface *surface = NULL;
    struct ps2_surface_exa exa = {0}; struct ps2_copy_window native = {0};
    void *privates[16] = {0};
    ScreenRec xs = {.GetScreenPixmap = get_front, .GetWindowPixmap = get_window,
        .CopyWindow = lower_copy, .CloseScreen = lower_close, .devPrivates = (void*)privates};
    PixmapRec fp = {0}, cp = {0}; WindowRec window = {0}; uint32_t canvas_handle = 0;
    RegionRec region; RegionNull(&region); RegionNull(&window.borderClip);
    front = backing = &fp;
    tty = open("/dev/tty0", O_RDWR|O_CLOEXEC);
    if (tty < 0 || ioctl(tty, KDGETMODE, &mode) || mode != KD_TEXT || ioctl(tty, KDSETMODE, KD_GRAPHICS)) goto out;
    graphics = 1; fb = open("/dev/fb0", O_RDWR|O_CLOEXEC); struct fb_var_screeninfo var;
    if (fb < 0 || ioctl(fb, FBIOGET_VSCREENINFO, &var)) goto out;
    var.xoffset = var.yoffset = 0; var.vmode &= ~FB_VMODE_YWRAP;
    if (ioctl(fb, FBIOPAN_DISPLAY, &var)) goto out;
    screen = ps2_pipe_screen_create("/dev/ps2-gs");
    if (!screen || !(ctx = screen->context_create(screen, NULL, 0))) goto out;
    struct ps2_render_client *render = ps2_pipe_context_render(ctx); struct ps2_surface_client *gs = render->gs;
    unsigned width = gs->caps.width, height = gs->caps.height;
    if (gs->caps.bits_per_pixel != 32 || width < 448 || height < 288) { errno = ENOTSUP; goto out; }
    printf("ps2-gs: native CopyWindow / shared Mesa %ux%u features=%x\n", width, height, gs->caps.features); fflush(stdout);
    struct pipe_resource t = {.target = PIPE_TEXTURE_2D, .format = PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0 = 129, .height0 = 65, .depth0 = 1, .array_size = 1, .bind = PIPE_BIND_SAMPLER_VIEW};
    source = screen->resource_create(screen, &t);
    t.width0 = 256; t.height0 = 192; t.bind = PIPE_BIND_RENDER_TARGET;
    canvas = screen->resource_create(screen, &t);
    if (!source || !canvas) goto out;
    struct pipe_surface st = {.format = canvas->format}; surface = ctx->create_surface(ctx, canvas, &st);
    if (!surface) goto out;
    struct pipe_framebuffer_state frame = {.width = 256, .height = 192, .nr_cbufs = 1, .cbufs = {surface}};
    ctx->set_framebuffer_state(ctx, &frame);
    fp.drawable = (DrawableRec){.type = DRAWABLE_PIXMAP, .pScreen = &xs, .depth = 24,
        .bitsPerPixel = 32, .width = width, .height = height};
    cp.drawable = (DrawableRec){.type = DRAWABLE_PIXMAP, .pScreen = &xs, .depth = 24,
        .bitsPerPixel = 32, .width = 256, .height = 192};
    canvas_handle = ((struct ps2_resource*)canvas)->handle; cp.devPrivates = (void*)&canvas_handle;
#ifdef COMPOSITE
    cp.screen_x = 100; cp.screen_y = 100;
#endif
    window.drawable = (DrawableRec){.type = DRAWABLE_WINDOW, .pScreen = &xs,
        .depth = 24, .bitsPerPixel = 32, .width = 129, .height = 65}; window.realized = TRUE;
    if (!ps2_surface_exa_prepare(&xs, &exa, gs) || !ps2_copy_window_prepare(&xs, &native, &exa, ctx)) goto out;
    uint32_t pixels[129*65]; static const uint32_t bars[] = {0x800000ff,0x8000ff00,0x80ff0000,0x80ffffff};
    /* This is client upload data. All destination drawing and moves are GS operations. */
    for (unsigned y = 0; y < 65; ++y) for (unsigned x = 0; x < 129; ++x)
        pixels[y*129+x] = !x || !y || x == 128 || y == 64 ? 0x80ffff00 : y < 10 ? 0x80ffff00 :
            y < 32 ? bars[(x-1)*4/128] : (((x/8+(y-32)/8)&1) ? 0x80ffffff : 0x80000000);
    struct pipe_box upload = {.width = 129, .height = 65, .depth = 1};
    ctx->texture_subdata(ctx, source, 0, 0, &upload, pixels, 129*4, 0);
    if (ps2_pipe_context_error(ctx)) goto out;
    int x = (width-448)/2, y = (height-288)/2, rx = x+248, ry = y+32;
    puts("ps2-gs: window scene 1/4: identical reference and window panels"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        present(ctx, ((struct ps2_resource*)source)->handle, 0, 0, x+24, ry, 129, 65) ||
        present(ctx, ((struct ps2_resource*)source)->handle, 0, 0, rx, ry, 129, 65) || pause_scene(seconds)) goto out;
    puts("ps2-gs: window scene 2/4: overlapping down/right move; original top/left edges remain"); fflush(stdout);
    window.drawable.x = rx+7; window.drawable.y = ry+5;
    RegionReset(&region, &(BoxRec){rx,ry,rx+129,ry+65});
    RegionReset(&window.borderClip, &(BoxRec){rx+7,ry+5,rx+136,ry+70});
    if (move(&window, (DDXPointRec){rx,ry}, &region) || region.extents.x1 != rx+7 || region.extents.y1 != ry+5 || pause_scene(seconds)) goto out;
    puts("ps2-gs: window scene 3/4: upper source and lower clipped copy with central vertical gap"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        present(ctx, ((struct ps2_resource*)source)->handle, 0, 0, x+24, ry, 129, 65) ||
        present(ctx, ((struct ps2_resource*)source)->handle, 0, 0, rx, ry, 129, 65)) goto out;
    window.drawable.x = rx+7; window.drawable.y = ry+112;
    RegionReset(&region, &(BoxRec){rx,ry,rx+129,ry+65});
    pixman_box16_t clips[] = {{rx+7,ry+112,rx+69,ry+177},{rx+73,ry+112,rx+136,ry+177}};
    RegionUninit(&window.borderClip);
    if (!pixman_region_init_rects(&window.borderClip, clips, 2) ||
        move(&window, (DDXPointRec){rx,ry}, &region) || region.extents.x1 != rx+7 || region.extents.y1 != ry+112 || pause_scene(seconds)) goto out;
    puts("ps2-gs: window scene 4/4: redirected backing pixmap move, presented beside reference"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        present(ctx, ((struct ps2_resource*)source)->handle, 0, 0, x+24, y+100, 129, 65)) goto out;
    union pipe_color_union navy = {.f = {6.0f/255,12.0f/255,32.0f/255,1}};
    ctx->clear(ctx, PIPE_CLEAR_COLOR0, NULL, &navy, 1, 0);
    ctx->resource_copy_region(ctx, canvas, 0, 20, 20, 0, source, 0, &upload);
    if (ps2_pipe_context_error(ctx)) goto out;
    backing = &cp;
    int ox = 0, oy = 0;
#ifdef COMPOSITE
    ox = cp.screen_x; oy = cp.screen_y;
#endif
    window.drawable.x = ox+27; window.drawable.y = oy+25;
    RegionReset(&region, &(BoxRec){ox+20,oy+20,ox+149,oy+85});
    RegionReset(&window.borderClip, &(BoxRec){ox+27,oy+25,ox+156,oy+90});
    if (move(&window, (DDXPointRec){ox+20,oy+20}, &region) ||
        present(ctx, canvas_handle, 0, 0, x+192, y+40, 256, 192) || pause_scene(seconds)) goto out;
    status = 0;
out:
    if (native.installed && !xs.CloseScreen(&xs)) status = 1;
    RegionUninit(&region); RegionUninit(&window.borderClip);
    if (surface) pipe_surface_reference(&surface, NULL);
    if (source) pipe_resource_reference(&source, NULL);
    if (canvas) pipe_resource_reference(&canvas, NULL);
    if (ctx) ctx->destroy(ctx);
    if (screen) screen->destroy(screen);
    if (fb >= 0) close(fb);
    if (graphics && ioctl(tty, KDSETMODE, KD_TEXT)) status = 1;
    if (tty >= 0) close(tty);
    if (callback_failure || stopped) status = 1;
    if (!status) puts("ps2-gs: native CopyWindow command/fence PASSED; window pixels require visual qualification");
    else fprintf(stderr, "ps2-gs: native CopyWindow probe FAILED: %s\n", strerror(errno ? errno : EIO));
    return status;
}
