/* SPDX-License-Identifier: MIT */
#include "ps2_context_bridge.h"
#include "ps2_coverage.h"
#include "ps2_rop.h"
#include "os.h"
#include "servermd.h"
static Bool coverage_upload(struct ps2_surface_exa *ps2,PixmapPtr pixmap,
    int x,int y,int width,int height,char *source,int pitch)
{
    struct ps2_exa_context_bridge *b=ps2->coverage_private;
    struct ps2_alpha_source bytes;
    if(!b || !b->attached || b->ordering.exa!=ps2 ||
        ps2_surface_exa_alpha_source(pixmap,&bytes)!=1)return FALSE;
    if(width<=0 || height<=0)return TRUE;
    size_t row=bytes.bits_per_pixel>=8?(size_t)width*(bytes.bits_per_pixel/8):((size_t)width+7)/8;
    if(x<0 || y<0 || !source || pitch<=0 || (size_t)pitch<row ||
        (unsigned)x>bytes.width || (unsigned)width>bytes.width-(unsigned)x ||
        (unsigned)y>bytes.height || (unsigned)height>bytes.height-(unsigned)y ||
        (size_t)(height-1)>(SIZE_MAX-row)/(size_t)pitch)
        FatalError("PS2 GS: invalid coverage upload\n");
    if(ps2_pipe_context_acquire(b->context))FatalError("PS2 GS: coverage upload context closed or failed\n");
    struct ps2_picture_bitmap target={.bits=bytes.bits,.bytes=bytes.bytes,.stride=bytes.stride,
        .width=bytes.width,.height=bytes.height,.bits_per_pixel=bytes.bits_per_pixel,.alpha_bits=bytes.depth,
        .msb_first=BITMAP_BIT_ORDER==MSBFirst};
    struct ps2_picture_bitmap input={.bits=(unsigned char*)source,
        .bytes=(size_t)(height-1)*pitch+row,.stride=pitch,.width=width,.height=height,
        .bits_per_pixel=bytes.bits_per_pixel,.alpha_bits=bytes.depth,.msb_first=BITMAP_BIT_ORDER==MSBFirst};
    struct ps2_coverage_surface d={0},s={0};
    struct ps2_pipe_copy_box box={0,0,x,y,width,height};
    if(ps2_coverage_decode_core(&d,ps2->gs,&target) ||
        ps2_coverage_decode_core(&s,ps2->gs,&input) ||
        ps2_pipe_rop_copy_boxes(b->context,s.surface,d.surface,&box,1,
            (1U<<bytes.depth)-1,GXcopy) ||
        ps2_coverage_encode(&d,b->context,(unsigned char*)bytes.bits,bytes.bytes,bytes.stride) ||
        ps2_coverage_release(&s) || ps2_coverage_release(&d))
        FatalError("PS2 GS: coverage upload failed; no CPU raster fallback\n");
    ps2_pipe_context_release(b->context);return TRUE;
}
Bool ps2_exa_context_bridge_prepare(ScreenPtr screen,struct ps2_exa_context_bridge *b,
    struct ps2_surface_exa *exa,struct pipe_context *context)
{
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    if (!screen || !b || !exa || !render || render->gs!=exa->gs || b->attached ||
        ps2_pipe_context_acquire(context)) return FALSE;
    if (!ps2_exa_render_bridge_prepare(screen,&b->ordering,exa,render)) {
        ps2_pipe_context_release(context); return FALSE;
    }
    b->context=context; b->attached=1;
    exa->coverage_upload=coverage_upload;exa->coverage_private=b;return TRUE;
}
void ps2_exa_context_bridge_cleanup(struct ps2_exa_context_bridge *b)
{
    if (!b || !b->attached) return;
    /* Restore the entire pre-attachment table, including removing any texture
     * callbacks installed afterwards. All operations are synchronous. The
     * bridge and later texture private structures stay screen-owned through
     * CloseScreen, but no restored callback reaches their released context. */
    b->ordering.exa->driver=b->ordering.original;
    b->ordering.exa->coverage_upload=NULL;b->ordering.exa->coverage_private=NULL;
    struct pipe_context *context=b->context;
    b->context=NULL; b->attached=0;
    ps2_pipe_context_release(context);
}
