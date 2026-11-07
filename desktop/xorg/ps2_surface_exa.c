/* SPDX-License-Identifier: MIT */
#include "ps2_surface_exa.h"
#include "ps2_gs_client.h"
#include "ps2_gs_logic.h"
#include "ps2-gs-map-blit.h"
#include "ps2_render_raster.h"
#include "os.h"
#include "servermd.h"
#ifdef PS2_DDX_DIAGNOSTICS
#include "dixstruct.h"
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static DevPrivateKeyRec ps2_surface_exa_key;
#define MAX_MONO_BYTES (1024U * 1024U)
#define MAX_MONO_PIXMAPS 1024U
/* Keep the color handle first for native adapters. Owned raw image bytes are
 * protocol storage; drawing decodes and encodes them on GS. */
struct ps2_exa_pixmap {
    uint32_t handle;
    unsigned char *mono;
    struct ps2_alpha_source borrowed;
    unsigned width, height, stride, bpp, depth;
    size_t bytes;
    struct ps2_exa_pixmap *next, **previous;
};

static struct ps2_surface_exa *backend(ScreenPtr screen)
{
    if (!screen || !screen->devPrivates) return NULL;
    return dixLookupPrivate(&screen->devPrivates, &ps2_surface_exa_key);
}
int ps2_surface_exa_alpha_source(PixmapPtr pixmap, struct ps2_alpha_source *source)
{
    if (!pixmap || !source || !pixmap->drawable.pScreen) return -1;
    struct ps2_surface_exa *ps2 = backend(pixmap->drawable.pScreen);
    if (!ps2) return 0;
    void *data = exaGetPixmapDriverPrivate(pixmap);
    for (struct ps2_exa_pixmap *p = ps2->monochrome; p; p = p->next) {
        if (p != data) continue;
        if(p->borrowed.bits)return 0;
        if (pixmap->drawable.type != DRAWABLE_PIXMAP || pixmap->drawable.depth != p->depth ||
            pixmap->drawable.bitsPerPixel != p->bpp || pixmap->drawable.width != p->width ||
            pixmap->drawable.height != p->height || pixmap->devKind != (int)p->stride ||
            (pixmap->devPrivate.ptr && pixmap->devPrivate.ptr != p->mono)) return -1;
        *source = (struct ps2_alpha_source){p->mono, p->bytes, p->stride, p->width, p->height, p->bpp,p->depth};
        return 1;
    }
    return 0;
}
int ps2_surface_exa_borrowed_source(PixmapPtr pixmap,struct ps2_alpha_source *out)
{
    if(!pixmap || !out || !pixmap->drawable.pScreen)return -1;
    struct ps2_surface_exa *b=backend(pixmap->drawable.pScreen);if(!b)return 0;
    void *data=exaGetPixmapDriverPrivate(pixmap);
    for(struct ps2_exa_pixmap *p=b->monochrome;p;p=p->next){
        if(p!=data || !p->borrowed.bits)continue;
        const struct ps2_alpha_source *s=&p->borrowed;
        if(pixmap->drawable.type!=DRAWABLE_PIXMAP || pixmap->drawable.x || pixmap->drawable.y ||
           pixmap->drawable.width!=s->width || pixmap->drawable.height!=s->height ||
           pixmap->drawable.depth!=s->depth || pixmap->drawable.bitsPerPixel!=s->bits_per_pixel ||
           pixmap->devKind!=(int)s->stride || (pixmap->devPrivate.ptr && pixmap->devPrivate.ptr!=s->bits))return -1;
        *out=*s;return 1;
    }
    return 0;
}
int ps2_surface_exa_mono_source(PixmapPtr pixmap, struct ps2_mono_source *source)
{
    if (!source) return -1;
    struct ps2_alpha_source alpha;
    int status=ps2_surface_exa_alpha_source(pixmap,&alpha);
    if(status!=1)return status;
    if(alpha.bits_per_pixel!=1)return 0;
    *source=(struct ps2_mono_source){alpha.bits,alpha.bytes,alpha.stride,alpha.width,alpha.height};
    return 1;
}
int ps2_surface_exa_raster_target(PixmapPtr pixmap,struct ps2_raster_target *target)
{
    struct ps2_alpha_source source;
    int status=ps2_surface_exa_alpha_source(pixmap,&source);
    if(status!=1 || !target)return status==1?-1:status;
    if(source.bits_per_pixel!=1 && source.bits_per_pixel!=8)return 0;
    struct ps2_exa_pixmap *p=exaGetPixmapDriverPrivate(pixmap);
    *target=(struct ps2_raster_target){p->mono,p->bytes,p->stride,p->width,p->height,
        p->depth,BITMAP_BIT_ORDER==MSBFirst};
    return 1;
}
static const struct ps2_surface *storage(PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    struct ps2_surface_exa *ps2 = backend(screen);
    if (!ps2) return NULL;
    uint32_t handle = 0;
    if (pixmap != screen->GetScreenPixmap(screen)) {
        struct ps2_exa_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
        if (!priv || !priv->handle) return NULL;
        handle = priv->handle;
    }
    const struct ps2_surface *buffer = ps2_surface_lookup(ps2->gs, handle);
    if (!buffer || pixmap->drawable.width != buffer->width || pixmap->drawable.height != buffer->height ||
        pixmap->drawable.bitsPerPixel != buffer->bits_per_pixel ||
        pixmap->drawable.depth != ps2_surface_depth(buffer)) return NULL;
    return buffer;
}
static void result(struct ps2_surface_exa *ps2, int status)
{
    if (status) FatalError("PS2 GS surface operation failed: %s; no CPU raster fallback\n", strerror(ps2->gs->error));
}
static void *create_pixmap(ScreenPtr screen, int width, int height, int depth,
                          int usage, int bpp, int *pitch)
{
    (void)usage;
    if (!pitch || width < 0 || height < 0 || width > 2048 || height > 2048 ||
        !((bpp == 1 && depth == 1) || (bpp == 8 && (depth == 4 || depth == 8)) ||
          (bpp == 16 && (depth == 12 || depth == 15 || depth == 16)) ||
          (bpp == 32 && (depth == 24 || depth == 30 || depth == 32)))) return NULL;
    struct ps2_surface_exa *ps2 = backend(screen);
    if (!ps2) return NULL;
    Bool raw=bpp<=8 || (bpp==16 && depth!=15);
    size_t stride = bpp == 1 ? (size_t)BitmapBytePad(width) : raw ? ((size_t)width*(bpp/8)+3)&~3U : 0;
    size_t bytes = stride * height;
    if (raw && (ps2->monochrome_count == MAX_MONO_PIXMAPS ||
        bytes > MAX_MONO_BYTES - ps2->monochrome_bytes)) return NULL;
    struct ps2_exa_pixmap *priv = calloc(1, sizeof(*priv));
    if (!priv) return NULL;
    *pitch = 0;
    if (raw) {
        if (bytes && !(priv->mono = calloc(1, bytes))) { free(priv); return NULL; }
        priv->width = width; priv->height = height; priv->stride = stride; priv->bytes = bytes;
        priv->bpp = bpp;priv->depth=depth;
        priv->next = ps2->monochrome; priv->previous = &ps2->monochrome;
        if (priv->next) priv->next->previous = &priv->next;
        ps2->monochrome = priv; ++ps2->monochrome_count; ps2->monochrome_bytes += bytes;
        *pitch = stride;
        return priv;
    }
    /* A zero-sized header can later name the front pixmap, with no allocation. */
    if (!width || !height) return priv;
    if(depth>=30 && !(ps2->gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)){free(priv);return NULL;}
    if (ps2_surface_create(ps2->gs, width, height, bpp, &priv->handle)) {
        free(priv); return NULL;
    }
    if(depth>=30 && ps2_surface_set_depth(ps2->gs,priv->handle,depth)){
        ps2_surface_destroy(ps2->gs,priv->handle);free(priv);return NULL;
    }
    const struct ps2_surface *buffer = ps2_surface_lookup(ps2->gs, priv->handle);
    *pitch = buffer->buffer_width * (bpp / 8);
    return priv;
}
static void destroy_pixmap(ScreenPtr screen, void *data)
{
    struct ps2_exa_pixmap *priv = data;
    struct ps2_surface_exa *ps2 = backend(screen);
    if (priv && priv->previous) {
        *priv->previous = priv->next;
        if (priv->next) priv->next->previous = priv->previous;
        --ps2->monochrome_count; ps2->monochrome_bytes -= priv->bytes;
        free(priv->mono);
    }
    if (priv && priv->handle && ps2->gs->fd >= 0 && !ps2->gs->error)
        ps2_surface_destroy(ps2->gs, priv->handle);
    free(priv);
}
static Bool modify_header(PixmapPtr pixmap, int width, int height, int depth,
                          int bpp, int pitch, void *pixels)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    struct ps2_surface_exa *ps2 = backend(screen);
    struct ps2_exa_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    /* EXA hides sys_ptr after ModifyPixmapHeader returns. Retain a read-only
     * descriptor for protocol staging, without exposing CPU raster access or
     * treating these bytes as an owned destination. Only empty raw headers
     * may be rebound; ordinary drawable storage remains immutable. */
    if(priv && priv->previous && !priv->mono && !priv->bytes && !priv->width && !priv->height && pixels){
        unsigned logical=depth>0?(unsigned)depth:priv->depth;
        unsigned actual=logical==1?1:(logical==4 || logical==8)?8:logical==16?16:0;
        /* The server's single scratch header is recycled across depths.
         * Its driver allocation is still empty and owns none of these bytes. */
        if(width<=0 || height<=0 || width>1024 || height>1024 || !actual ||
           (bpp>0 && (unsigned)bpp!=actual && !(logical==4 && bpp==4)))return FALSE;
        size_t row=actual==1?((unsigned)width+7)/8:(size_t)width*(actual/8);
        size_t stride=pitch>0?(size_t)pitch:(((size_t)width*actual+31)/32)*4;
        if(stride<row || stride>MAX_MONO_BYTES/(unsigned)height)return FALSE;
        size_t bytes=stride*(unsigned)height;
        if((uintptr_t)pixels>UINTPTR_MAX-(bytes-1))return FALSE;
        priv->borrowed=(struct ps2_alpha_source){pixels,bytes,stride,width,height,actual,logical};
        pixmap->drawable.width=width;pixmap->drawable.height=height;pixmap->drawable.depth=logical;
        pixmap->drawable.bitsPerPixel=actual;pixmap->devKind=stride;pixmap->devPrivate.ptr=NULL;return TRUE;
    }
    if(priv && priv->borrowed.bits && !pixels)memset(&priv->borrowed,0,sizeof(priv->borrowed));

    if (priv && priv->previous) {
        if ((width > 0 && (unsigned)width != priv->width) ||
            (height > 0 && (unsigned)height != priv->height) ||
            (depth > 0 && depth != (int)priv->depth) || (bpp > 0 && bpp != (int)priv->bpp) ||
            (pitch > 0 && (unsigned)pitch != priv->stride) || (pixels && pixels != priv->mono)) return FALSE;
        pixmap->drawable.width = priv->width; pixmap->drawable.height = priv->height;
        pixmap->drawable.depth = priv->depth; pixmap->drawable.bitsPerPixel = priv->bpp;
        pixmap->devKind = priv->stride; pixmap->devPrivate.ptr = priv->mono;
        return TRUE;
    }
    const struct ps2_surface *buffer = NULL;
    if (pixmap == screen->GetScreenPixmap(screen))
        buffer = &ps2->gs->front;
    else if (priv && priv->handle)
        buffer = ps2_surface_lookup(ps2->gs, priv->handle);
    if (!buffer || pixels) return FALSE;
    if (width <= 0) width = pixmap->drawable.width;
    if (height <= 0) height = pixmap->drawable.height;
    if (depth <= 0) depth = pixmap->drawable.depth;
    if (bpp <= 0) bpp = pixmap->drawable.bitsPerPixel;
    /* Swizzled GS storage has no CPU pixel pointer. Resizing needs a new
     * resource, not changing the descriptor of an existing allocation. */
    if ((unsigned)width != buffer->width || (unsigned)height != buffer->height ||
        (unsigned)depth != ps2_surface_depth(buffer) || (unsigned)bpp != buffer->bits_per_pixel ||
        (pitch > 0 && buffer->handle && (unsigned)pitch != buffer->buffer_width * (buffer->bits_per_pixel / 8))) return FALSE;
    pixmap->drawable.width = width; pixmap->drawable.height = height;
    pixmap->drawable.depth = depth; pixmap->drawable.bitsPerPixel = bpp;
    pixmap->devKind = pitch > 0 ? pitch : ((width+63)&~63) * (bpp/8);
    pixmap->devPrivate.ptr = NULL;
    return TRUE;
}
static Bool is_offscreen(PixmapPtr pixmap)
{
    struct ps2_alpha_source alpha;
    /* EXA_HANDLES_PIXMAPS asks whether driver callbacks can consume this
     * storage. Owned A1 bits are valid native composite sources, even though
     * they have no GS handle. Returning false makes EXA reject the mask before
     * PrepareComposite and attempt CPU access to the colour destination.
     * Every drawing callback validates its storage; PrepareAccess rejects
     * CPU raster access for every depth. */
    return storage(pixmap) != NULL || ps2_surface_exa_alpha_source(pixmap, &alpha) == 1;
}
static Bool prepare_solid(PixmapPtr pixmap, int alu, Pixel mask, Pixel color)
{
    const struct ps2_surface *buffer = storage(pixmap);
    if (!buffer || alu != GXcopy ||
        ((uint32_t)mask & ps2_surface_native_mask(backend(pixmap->drawable.pScreen)->gs,buffer->handle)) !=
        ps2_surface_native_mask(backend(pixmap->drawable.pScreen)->gs,buffer->handle)) return FALSE;
    backend(pixmap->drawable.pScreen)->color = buffer->pixel_depth>=30?(uint32_t)color&ps2_surface_native_mask(backend(pixmap->drawable.pScreen)->gs,buffer->handle):ps2_gs_pixel_color(color, buffer->bits_per_pixel);
    return TRUE;
}
static void solid(PixmapPtr pixmap, int x1, int y1, int x2, int y2)
{
    if (x2 <= x1 || y2 <= y1) return;
    if (x1 < 0 || y1 < 0) FatalError("PS2 GS: unclipped X solid\n");
    const struct ps2_surface *buffer = storage(pixmap);
    if (!buffer) FatalError("PS2 GS: X pixmap has no owned storage\n");
    struct ps2_surface_exa *ps2 = backend(pixmap->drawable.pScreen);
    result(ps2, buffer->pixel_depth==30?ps2_surface_solid_logic(ps2->gs,buffer->handle,x1,y1,x2-x1,y2-y1,ps2->color,0x3fffffff,3):
        ps2_surface_fill(ps2->gs, buffer->handle, x1, y1, x2-x1, y2-y1, ps2->color));
}
static Bool prepare_copy(PixmapPtr source, PixmapPtr destination, int xdir, int ydir, int alu, Pixel mask)
{
    (void)xdir; (void)ydir;
    const struct ps2_surface *s = storage(source), *d = storage(destination);
    if (source->drawable.pScreen != destination->drawable.pScreen || !s || !d ||
        s->bits_per_pixel != d->bits_per_pixel || alu != GXcopy ||
        d->pixel_depth>=30 ||
        ((uint32_t)mask & ps2_surface_native_mask(backend(destination->drawable.pScreen)->gs,d->handle)) !=
        ps2_surface_native_mask(backend(destination->drawable.pScreen)->gs,d->handle)) return FALSE;
    backend(destination->drawable.pScreen)->source = s->handle;
    return TRUE;
}
static void copy(PixmapPtr pixmap, int sx, int sy, int dx, int dy, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0) FatalError("PS2 GS: unclipped X copy\n");
    const struct ps2_surface *buffer = storage(pixmap);
    if (!buffer) FatalError("PS2 GS: X pixmap has no owned storage\n");
    struct ps2_surface_exa *ps2 = backend(pixmap->drawable.pScreen);
    result(ps2, ps2_surface_copy(ps2->gs, ps2->source, buffer->handle, sx, sy, dx, dy, width, height));
}
static Bool upload(PixmapPtr pixmap, int x, int y, int width, int height, char *source, int pitch)
{
    const struct ps2_surface *buffer = storage(pixmap);
    if (!buffer) {
        struct ps2_surface_exa *ps2=backend(pixmap->drawable.pScreen);
        return ps2 && ps2->coverage_upload?
            ps2->coverage_upload(ps2,pixmap,x,y,width,height,source,pitch):FALSE;
    }
    if (width <= 0 || height <= 0) return TRUE;
    if (x < 0 || y < 0 || !source || pitch <= 0 || (unsigned)width > buffer->width ||
        (unsigned)height > buffer->height) FatalError("PS2 GS: invalid X image dimensions\n");
    size_t row = (size_t)width * (buffer->bits_per_pixel / 8);
    if ((size_t)pitch < row || (size_t)(height-1) > (SIZE_MAX-row)/(size_t)pitch)
        FatalError("PS2 GS: invalid X image stride\n");
    struct ps2_surface_exa *ps2 = backend(pixmap->drawable.pScreen);
    result(ps2, ps2_surface_upload(ps2->gs, buffer->handle, x, y, width, height,
        source, (size_t)(height-1)*pitch+row, pitch));
    return TRUE;
}
static Bool prepare_access(PixmapPtr pixmap, int index)
{
    (void)index;
#ifdef PS2_DDX_DIAGNOSTICS
    ClientPtr client=GetCurrentClient();
    unsigned request=client && client->requestBuffer?((xReq*)client->requestBuffer)->reqType:0;
    ErrorF("PS2 GS: rejected access during client=%d request=%u\n",client?client->index:0,request);
#endif
    FatalError("PS2 GS: CPU raster access rejected: depth=%u size=%ux%u index=%d caller=%p\n",
        pixmap->drawable.depth,pixmap->drawable.width,pixmap->drawable.height,index,__builtin_return_address(0));
    return FALSE;
}
static void finish_access(PixmapPtr pixmap, int index)
{
    (void)index;
    pixmap->devPrivate.ptr = NULL;
}
static void done(PixmapPtr pixmap) { (void)pixmap; /* Every surface operation is synchronous. */ }
static void wait_marker(ScreenPtr screen, int marker) { (void)screen; (void)marker; }

Bool ps2_surface_exa_prepare(ScreenPtr screen, struct ps2_surface_exa *ps2, struct ps2_surface_client *gs)
{
    if (!dixRegisterPrivateKey(&ps2_surface_exa_key, PRIVATE_SCREEN, 0)) return FALSE;
    memset(ps2, 0, sizeof(*ps2)); ps2->gs = gs;
    ps2->driver.exa_major = EXA_VERSION_MAJOR; ps2->driver.exa_minor = EXA_VERSION_MINOR;
    ps2->driver.flags = EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS;
    /* EXA computes header pitches even with driver-owned pixmaps. Zero would
     * divide by zero in exaSetFbPitch. Native GS colour allocation chooses its
     * own 64-pixel stride; source bitmap rows use Xorg's four-byte padding. */
    ps2->driver.pixmapPitchAlign = 4; ps2->driver.pixmapOffsetAlign = 256;
    ps2->driver.maxX = 2048; ps2->driver.maxY = 2048; ps2->driver.maxPitchBytes = 8192;
    ps2->driver.CreatePixmap2 = create_pixmap; ps2->driver.DestroyPixmap = destroy_pixmap;
    ps2->driver.ModifyPixmapHeader = modify_header; ps2->driver.PixmapIsOffscreen = is_offscreen;
    ps2->driver.PrepareSolid = prepare_solid; ps2->driver.Solid = solid; ps2->driver.DoneSolid = done;
    ps2->driver.PrepareCopy = prepare_copy; ps2->driver.Copy = copy; ps2->driver.DoneCopy = done;
    ps2->driver.UploadToScreen = upload; ps2->driver.PrepareAccess = prepare_access;
    ps2->driver.FinishAccess = finish_access;
    ps2->driver.WaitMarker = wait_marker;
    dixSetPrivate(&screen->devPrivates, &ps2_surface_exa_key, ps2);
    return TRUE;
}
