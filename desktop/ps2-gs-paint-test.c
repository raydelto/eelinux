/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "util/u_inlines.h"
#undef ARRAY_SIZE
#include "ps2_paint_window.h"
#include "ps2_blit.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
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

/* Actual native PaintWindow and Mesa tile/solid operations. Minimal screen
 * bookkeeping and Damage callback stubs; this is not an X server or TWM. */
BoxRec RegionEmptyBox = {0};
RegDataRec RegionEmptyData = {0}, RegionBrokenData = {0};
static unsigned private_slots, damage_appends, damage_pending;
static PixmapPtr front, backing;
static volatile sig_atomic_t stopped;
static jmp_buf abort_callback;
static int callback_failure;
Bool dixRegisterPrivateKey(DevPrivateKey key, DevPrivateType type, unsigned size)
{
    if (size || type != PRIVATE_SCREEN) return FALSE;
    if (!key->initialized) {
        if (private_slots == 16) return FALSE;
        *key = (DevPrivateKeyRec){.initialized = TRUE, .type = type,
            .offset = private_slots++*sizeof(void*)};
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
void DamageRegionAppend(DrawablePtr d, RegionPtr r)
{
    if (!d || !r || RegionBroken(r) || !RegionNumRects(r))
        FatalError("ps2-gs: invalid diagnostic Damage notification\n");
    ++damage_appends;
}
void DamageRegionProcessPending(DrawablePtr d) { (void)d; ++damage_pending; }
static PixmapPtr get_front(ScreenPtr s) { (void)s; return front; }
static PixmapPtr get_window(WindowPtr w) { (void)w; return backing; }
static void lower_paint(WindowPtr w, RegionPtr r, int what)
{ (void)w; (void)r; (void)what; FatalError("ps2-gs: unexpected lower window painting\n"); }
static Bool lower_close(ScreenPtr s) { return s->PaintWindow == lower_paint; }
static void interrupt(int sig) { (void)sig; stopped = 1; }
static int pause_scene(unsigned seconds)
{
    struct timespec delay = {.tv_sec = seconds};
    while (!stopped && nanosleep(&delay, &delay)) if (errno != EINTR) return -1;
    return stopped ? -1 : 0;
}
static int paint(WindowPtr w, RegionPtr r, int what)
{
    if (setjmp(abort_callback)) return -1;
    w->drawable.pScreen->PaintWindow(w, r, what); return 0;
}
/* All boxes are screen coordinates, including those targeting an owned pixmap. */
static int panel(ScreenPtr screen, PixmapPtr tile, PixmapPtr border_tile,
    int x, int y, int width, int height, int inherited)
{
    WindowRec ancestor = {0}, middle = {0}, child = {0};
    ancestor.drawable = (DrawableRec){.type = DRAWABLE_WINDOW, .pScreen = screen,
        .depth = 24, .bitsPerPixel = 32, .x = x-11, .y = y-9};
    ancestor.backgroundState = BackgroundPixmap; ancestor.background.pixmap = tile;
    middle.drawable = ancestor.drawable; middle.backgroundState = ParentRelative;
    middle.parent = &ancestor;
    child.drawable = (DrawableRec){.type = DRAWABLE_WINDOW, .pScreen = screen,
        .depth = 24, .bitsPerPixel = 32, .x = x+8, .y = y+8,
        .width = width-16, .height = height-16}; child.realized = TRUE;
    child.backgroundState = inherited ? ParentRelative : tile ? BackgroundPixmap : BackgroundPixel;
    if (tile) child.background.pixmap = tile; else child.background.pixel = 0x00ffff00;
    child.parent = &middle; child.borderIsPixel = border_tile ? FALSE : TRUE;
    if (border_tile) child.border.pixmap = border_tile;
    else child.border.pixel = tile ? 0x00ffff00 : 0x00ffffff;
    BoxRec outer_box = {x,y,x+width,y+height}, inner_box = {x+8,y+8,x+width-8,y+height-8};
    RegionRec outer, inner, border;
    RegionInit(&outer, &outer_box, 1); RegionInit(&inner, &inner_box, 1);
    RegionNull(&border); RegionNull(&child.clipList); int result = -1;
    if (!RegionSubtract(&border, &outer, &inner) || paint(&child, &border, PW_BORDER)) goto out;
    if (inherited) {
        /* Adjacent children have different non-periodic origins. Their inherited
         * tile must remain continuous across the join rather than restart. */
        int split = inner_box.x1 + (width-16)/2 + 3;
        BoxRec left = {inner_box.x1,inner_box.y1,split,inner_box.y2};
        RegionReset(&child.clipList, &left);
        if (paint(&child, &inner, PW_BACKGROUND)) goto out;
        child.drawable.x = split; child.drawable.width = inner_box.x2-split;
        BoxRec right = {split,inner_box.y1,inner_box.x2,inner_box.y2};
        RegionReset(&child.clipList, &right);
        if (paint(&child, &inner, PW_BACKGROUND)) goto out;
    } else {
        RegionReset(&child.clipList, &inner_box);
        if (paint(&child, &inner, PW_BACKGROUND)) goto out;
    }
    result = 0;
out:
    RegionUninit(&child.clipList); RegionUninit(&border); RegionUninit(&inner); RegionUninit(&outer);
    return result;
}
int main(int argc, char **argv)
{
    unsigned seconds = 3;
    if (argc == 2 && !strcmp(argv[1], "--brief")) seconds = 1;
    else if (argc != 1) { fputs("usage: ps2-gs-paint-test [--brief]\n", stderr); return 2; }
    stopped = 0; callback_failure = 0; damage_appends = damage_pending = 0;
    struct sigaction action = {.sa_handler = interrupt}; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL) || sigaction(SIGHUP, &action, NULL)) return 1;
    int tty = -1, fb = -1, graphics = 0, status = 1, mode;
    struct pipe_screen *screen = NULL; struct pipe_context *ctx = NULL;
    struct pipe_resource *checker = NULL, *stripes = NULL, *canvas = NULL;
    struct ps2_surface_exa exa = {0}; struct ps2_paint_window native = {0};
    void *privates[16] = {0};
    ScreenRec xs = {.GetScreenPixmap = get_front, .GetWindowPixmap = get_window,
        .PaintWindow = lower_paint, .CloseScreen = lower_close, .devPrivates = (void*)privates};
    PixmapRec fp = {0}, cp = {0}, tp = {0}, bp = {0};
    uint32_t canvas_handle = 0, checker_handle = 0, stripes_handle = 0;
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
    printf("ps2-gs: native PaintWindow / shared Mesa %ux%u features=%x\n", width, height, gs->caps.features); fflush(stdout);
    struct pipe_resource t = {.target = PIPE_TEXTURE_2D, .format = PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0 = 32, .height0 = 32, .depth0 = 1, .array_size = 1, .bind = PIPE_BIND_SAMPLER_VIEW};
    checker = screen->resource_create(screen, &t);
    t.width0 = 21; t.height0 = 15; stripes = screen->resource_create(screen, &t);
    t.width0 = 192; t.height0 = 128; t.bind = PIPE_BIND_RENDER_TARGET; canvas = screen->resource_create(screen, &t);
    if (!checker || !stripes || !canvas) goto out;
    fp.drawable = (DrawableRec){.type = DRAWABLE_PIXMAP, .pScreen = &xs, .depth = 24,
        .bitsPerPixel = 32, .width = width, .height = height};
    cp.drawable = tp.drawable = bp.drawable = fp.drawable;
    cp.drawable.width = 192; cp.drawable.height = 128;
    tp.drawable.width = tp.drawable.height = 32; bp.drawable.width = 21; bp.drawable.height = 15;
    canvas_handle = ((struct ps2_resource*)canvas)->handle; cp.devPrivates = (void*)&canvas_handle;
    checker_handle = ((struct ps2_resource*)checker)->handle; tp.devPrivates = (void*)&checker_handle;
    stripes_handle = ((struct ps2_resource*)stripes)->handle; bp.devPrivates = (void*)&stripes_handle;
#ifdef COMPOSITE
    cp.screen_x = 100; cp.screen_y = 100;
#endif
    if (!ps2_surface_exa_prepare(&xs, &exa, gs) || !ps2_paint_window_prepare(&xs, &native, &exa, ctx)) goto out;
    /* Client tile uploads only; backgrounds, borders and presentation are GS work. */
    uint32_t pixels[32*32];
    for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x)
        pixels[y*32+x] = ((x/16+y/16)&1) ? 0x80ffffff : 0x80000000;
    struct pipe_box upload = {.width = 32, .height = 32, .depth = 1};
    ctx->texture_subdata(ctx, checker, 0, 0, &upload, pixels, 32*4, 0);
    for (unsigned y = 0; y < 15; ++y) for (unsigned x = 0; x < 21; ++x)
        pixels[y*21+x] = ((x/7+y/5)&1) ? 0x80ffff00 : 0x8000ffff;
    upload.width = 21; upload.height = 15;
    ctx->texture_subdata(ctx, stripes, 0, 0, &upload, pixels, 21*4, 0);
    if (ps2_pipe_context_error(ctx)) goto out;
    int x = (width-448)/2, y = (height-288)/2;
    puts("ps2-gs: paint scene 1/4: solid cyan background with white border"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        panel(&xs, NULL, NULL, x+96, y+80, 256, 128, 0) || pause_scene(seconds)) goto out;
    puts("ps2-gs: paint scene 2/4: repeated checker background with cyan border"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        panel(&xs, &tp, NULL, x+32, y+64, 384, 160, 0) || pause_scene(seconds)) goto out;
    puts("ps2-gs: paint scene 3/4: continuous inherited checker across child join; yellow/cyan tiled border"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        panel(&xs, &tp, &bp, x+32, y+64, 384, 160, 1) || pause_scene(seconds)) goto out;
    puts("ps2-gs: paint scene 4/4: matching front and redirected backing-pixmap panels"); fflush(stdout);
    if (ps2_surface_fill(gs, 0, 0, 0, width, height, 0x80200c06) ||
        panel(&xs, &tp, &bp, x+16, y+80, 192, 128, 1)) goto out;
    backing = &cp; int ox = 0, oy = 0;
#ifdef COMPOSITE
    ox = cp.screen_x; oy = cp.screen_y;
#endif
    const struct ps2_pipe_blit_rect present = {.sw = 192, .sh = 128,
        .dx = x+240, .dy = y+80, .dw = 192, .dh = 128};
    if (panel(&xs, &tp, &bp, ox, oy, 192, 128, 1) ||
        ps2_pipe_blit_rect(ctx, canvas_handle, 0, &present) || pause_scene(seconds)) goto out;
    if (damage_appends != 13 || damage_pending != 13) { errno = EIO; goto out; }
    status = 0;
out:
    if (native.installed && !xs.CloseScreen(&xs)) status = 1;
    if (checker) pipe_resource_reference(&checker, NULL);
    if (stripes) pipe_resource_reference(&stripes, NULL);
    if (canvas) pipe_resource_reference(&canvas, NULL);
    if (ctx) ctx->destroy(ctx);
    if (screen) screen->destroy(screen);
    if (fb >= 0) close(fb);
    if (graphics && ioctl(tty, KDSETMODE, KD_TEXT)) status = 1;
    if (tty >= 0) close(tty);
    if (callback_failure || stopped) status = 1;
    if (!status) puts("ps2-gs: native PaintWindow command/fence PASSED; paint pixels require visual qualification");
    else fprintf(stderr, "ps2-gs: native PaintWindow probe FAILED: %s\n", strerror(errno ? errno : EIO));
    return status;
}
