/* SPDX-License-Identifier: MIT */
#include "ps2_cursor_screen.h"
#include "ps2_cursor.h"
#include "ps2_gs_render.h"
#include "cursorstr.h"
#include "inputstr.h"
#include "pixmapstr.h"
#include "servermd.h"
#include "os.h"
#include <errno.h>
#include <string.h>
static DevPrivateKeyRec key;
static Bool close_screen(ScreenPtr);
static void block_handler(ScreenPtr,void *);
static void best_size(int,unsigned short *,unsigned short *,ScreenPtr);
static struct ps2_cursor_screen *owner(ScreenPtr s)
{
    struct ps2_cursor_screen *b=s?dixLookupPrivate(&s->devPrivates,&key):NULL;
    if(!b || !b->installed)FatalError("PS2 GS: missing native cursor owner\n");
    return b;
}
static void result(int status)
{ if(status)FatalError("PS2 GS: native cursor failed: %s\n",strerror(errno)); }
static DeviceIntPtr pointer_owner(DeviceIntPtr device)
{
    /* MI's sprite is shared by a master and its attached slave devices.
     * Physical motion callbacks may carry the slave, whereas WarpPointer
     * carries the master. Both address the same native screen cursor. */
    return device?GetMaster(device,MASTER_POINTER):NULL;
}
static Bool supported(CursorPtr cursor)
{
    CursorBitsPtr b=cursor?cursor->bits:NULL;
    if(b && b->argb)return b->width && b->height && b->width<=64 && b->height<=64 &&
        b->xhot<b->width && b->yhot<b->height;
    /* Xorg has already checked the mask. A fully transparent glyph cursor
     * can have zero dimensions and no bitmap; xterm uses it to hide its pointer. */
    if(b && !b->argb && b->emptyMask)return TRUE;
    return b && !b->argb && b->source && b->mask && b->width && b->height &&
        b->width<=64 && b->height<=64 && b->xhot<b->width && b->yhot<b->height;
}
static Bool realize(DeviceIntPtr d,ScreenPtr s,CursorPtr cursor)
{ (void)d;(void)s;return supported(cursor); }
static Bool unrealize(DeviceIntPtr d,ScreenPtr s,CursorPtr cursor)
{
    (void)d;struct ps2_cursor_screen *b=owner(s);
    if(b->current==cursor){result(ps2_pipe_cursor_set(b->native,NULL,0,0));b->current=NULL;}
    return TRUE;
}
static uint32_t pixel(unsigned r,unsigned g,unsigned b,unsigned bpp)
{ return bpp==16?(r>>11)|((g>>11)<<5)|((b>>11)<<10):(r>>8)|((g>>8)<<8)|((b>>8)<<16); }
static void set_cursor(DeviceIntPtr d,ScreenPtr s,CursorPtr cursor,int x,int y)
{
    struct ps2_cursor_screen *b=owner(s);
    if(!b->device || pointer_owner(d)!=b->device)FatalError("PS2 GS: multiple cursor masters unsupported\n");
    if(!cursor){result(ps2_pipe_cursor_set(b->native,NULL,x,y));b->current=NULL;return;}
    if(!supported(cursor))FatalError("PS2 GS: unsupported colour/oversized cursor\n");
    if(cursor->bits->argb){
        CursorBitsPtr bits=cursor->bits;
        result(ps2_pipe_cursor_set_argb(b->native,bits->argb,(size_t)bits->width*bits->height*4,
            bits->width,bits->height,bits->xhot,bits->yhot,x,y));
        b->current=cursor;return;
    }
    if(cursor->bits->emptyMask){
        result(ps2_pipe_cursor_set(b->native,NULL,x,y));b->current=cursor;return;
    }
    CursorBitsPtr bits=cursor->bits;size_t stride=BitmapBytePad(bits->width);
    /* Screen depth, fixed by native DDX setup, determines the logical pixels. */
    unsigned bpp=s->rootDepth==15?16:32;
    struct ps2_cursor_image image={.source=bits->source,.mask=bits->mask,
        .source_bytes=stride*bits->height,.mask_bytes=stride*bits->height,.stride=stride,
        .width=bits->width,.height=bits->height,.xhot=bits->xhot,.yhot=bits->yhot,
        .msb_first=BITMAP_BIT_ORDER==MSBFirst,
        .foreground=pixel(cursor->foreRed,cursor->foreGreen,cursor->foreBlue,bpp),
        .background=pixel(cursor->backRed,cursor->backGreen,cursor->backBlue,bpp)};
    result(ps2_pipe_cursor_set(b->native,&image,x,y));b->current=cursor;
}
static void move_cursor(DeviceIntPtr d,ScreenPtr s,int x,int y)
{
    struct ps2_cursor_screen *b=owner(s);
    if(!b->device || pointer_owner(d)!=b->device)FatalError("PS2 GS: foreign cursor master (device=%d owner=%d)\n",d?d->id:-1,b->device?b->device->id:-1);
    result(ps2_pipe_cursor_move(b->native,x,y));
}
static Bool device_initialize(DeviceIntPtr d,ScreenPtr s)
{
    struct ps2_cursor_screen *b=owner(s);
    DeviceIntPtr master=pointer_owner(d);
    if(!master || (b->device && b->device!=master))return FALSE;
    b->device=master;return TRUE;
}
static void device_cleanup(DeviceIntPtr d,ScreenPtr s)
{
    struct ps2_cursor_screen *b=owner(s);
    if(b->device!=d)return;
    result(ps2_pipe_cursor_set(b->native,NULL,0,0));b->device=NULL;b->current=NULL;
}
static const miPointerSpriteFuncRec sprite_funcs={realize,unrealize,set_cursor,move_cursor,
    device_initialize,device_cleanup};
static void damaged(DamagePtr damage,RegionPtr region,void *closure)
{
    struct ps2_cursor_screen *b=closure;
    if(!region)result(ps2_pipe_cursor_invalidate(b->native));
    else{
        BoxPtr boxes=RegionRects(region);
        for(int i=0;i<RegionNumRects(region);++i){
            int x=boxes[i].x1<0?0:boxes[i].x1,y=boxes[i].y1<0?0:boxes[i].y1;
            if(boxes[i].x2>x && boxes[i].y2>y)
                result(ps2_pipe_cursor_invalidate_region(b->native,x,y,boxes[i].x2-x,boxes[i].y2-y));
        }
    }
    DamageEmpty(damage);
}
void ps2_cursor_screen_refresh(ScreenPtr s)
{
    if(!s || !dixPrivateKeyRegistered(&key))return;
    struct ps2_cursor_screen *b=dixLookupPrivate(&s->devPrivates,&key);
    if(b && b->installed)result(ps2_pipe_cursor_flush(b->native));
}
static void damage_destroyed(DamagePtr damage,void *closure)
{
    struct ps2_cursor_screen *b=closure;
    if(b->damage==damage)b->damage=NULL;
}
static void best_size(int kind,unsigned short *w,unsigned short *h,ScreenPtr s)
{
    struct ps2_cursor_screen *b=owner(s);
    if(kind==CursorShape){if(*w>64)*w=64;if(*h>64)*h=64;return;}
    b->QueryBestSize(kind,w,h,s);
}
static void block_handler(ScreenPtr s,void *timeout)
{
    struct ps2_cursor_screen *b=owner(s);
    if(s->BlockHandler!=block_handler)FatalError("PS2 GS: cursor block owner changed\n");
    s->BlockHandler=b->BlockHandler;s->BlockHandler(s,timeout);
    b->BlockHandler=s->BlockHandler;s->BlockHandler=block_handler;
    result(ps2_pipe_cursor_flush(b->native));
}
Bool ps2_cursor_screen_prepare(ScreenPtr s,struct ps2_cursor_screen *b,
    struct pipe_context *context,miPointerScreenFuncPtr screen_funcs)
{
    struct ps2_render_client *r=ps2_pipe_context_render(context);
    if(!s || !b || b->installed || !r || !screen_funcs || !screen_funcs->CursorOffScreen ||
       !screen_funcs->CrossScreen || !screen_funcs->WarpCursor || !s->CloseScreen ||
       !s->BlockHandler || !s->QueryBestSize || !s->GetScreenPixmap ||
       !((s->rootDepth==24 && r->gs->front.bits_per_pixel==32) ||
         (s->rootDepth==15 && r->gs->front.bits_per_pixel==16)) ||
       !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&s->devPrivates,&key))return FALSE;
    PixmapPtr front=s->GetScreenPixmap(s);
    if(!front || front->drawable.pScreen!=s || front->drawable.width!=r->gs->front.width ||
       front->drawable.height!=r->gs->front.height || front->drawable.depth!=s->rootDepth ||
       front->devPrivate.ptr)return FALSE;
    struct ps2_pipe_cursor *native=ps2_pipe_cursor_create(context);if(!native)return FALSE;
    *b=(struct ps2_cursor_screen){.screen=s,.native=native,.screen_funcs=*screen_funcs};
    b->damage=DamageCreate(damaged,damage_destroyed,DamageReportRawRegion,TRUE,s,b);
    if(!b->damage){ps2_pipe_cursor_destroy(native);memset(b,0,sizeof(*b));return FALSE;}
    DamageSetReportAfterOp(b->damage,FALSE);
    dixSetPrivate(&s->devPrivates,&key,b);b->installed=TRUE;
    if(!miPointerInitialize(s,(miPointerSpriteFuncPtr)&sprite_funcs,&b->screen_funcs,TRUE)){
        b->installed=FALSE;dixSetPrivate(&s->devPrivates,&key,NULL);
        DamageDestroy(b->damage);ps2_pipe_cursor_destroy(native);memset(b,0,sizeof(*b));return FALSE;
    }
    DamageRegister(&front->drawable,b->damage);
    b->CloseScreen=s->CloseScreen;b->BlockHandler=s->BlockHandler;b->QueryBestSize=s->QueryBestSize;
    s->CloseScreen=close_screen;s->BlockHandler=block_handler;s->QueryBestSize=best_size;return TRUE;
}
Bool ps2_cursor_screen_suspend(struct ps2_cursor_screen *b,Bool suspend)
{ return b && b->installed && !ps2_pipe_cursor_suspend(b->native,!!suspend); }
static Bool close_screen(ScreenPtr s)
{
    struct ps2_cursor_screen *b=owner(s);
    if(s->CloseScreen!=close_screen || s->BlockHandler!=block_handler || s->QueryBestSize!=best_size)
        FatalError("PS2 GS: cursor callbacks changed before close\n");
    s->CloseScreen=b->CloseScreen;s->BlockHandler=b->BlockHandler;s->QueryBestSize=b->QueryBestSize;
    if(b->damage){DamageUnregister(b->damage);DamageDestroy(b->damage);}
    int status=ps2_pipe_cursor_destroy(b->native);
    if(status && errno==EBUSY)result(status); /* A foreign observer still owns it. */
    b->native=NULL;b->current=NULL;b->device=NULL;
    b->installed=FALSE;dixSetPrivate(&s->devPrivates,&key,NULL);
    Bool lower=s->CloseScreen(s);
    return !status && lower;
}
