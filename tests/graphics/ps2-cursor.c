/* SPDX-License-Identifier: MIT */
#include "previous-copy.c"
#include "ps2_cursor.h"
#include "ps2_composite.h"
#include "ps2_solid_batch.h"
#include "ps2_cursor_screen.h"
#include "cursorstr.h"
#include "inputstr.h"
#include "ps2-line-bias-stub.h"
RegDataRec RegionBrokenData={0};
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{return original_register_private(key,type,size);}
void DamageRegionAppend(DrawablePtr d,RegionPtr r){previous_damage_append(d,r);}
void DamageRegionProcessPending(DrawablePtr d){previous_damage_process(d);}
static unsigned cursor_solids,attempts;
/* Monochrome-only component fixtures do not link the colour compositor.
 * Fixtures that link its production implementation override this weak stub. */
__attribute__((weak)) int ps2_pipe_composite_rect(struct pipe_context *context,uint32_t source,uint32_t destination,
 const struct ps2_pipe_copy_box *box,const struct ps2_picture_sample *sample,const struct ps2_picture_bitmap *mask,unsigned operation)
{(void)context;(void)source;(void)destination;(void)box;(void)sample;(void)mask;(void)operation;errno=ENOTSUP;return -1;}
static int reject_texture;
static int fail_at,fail_damage,fail_pointer;
static miPointerSpriteFuncPtr sprites;
static CloseScreenProcPtr pointer_lower;
static struct { DamageReportFunc report;DamageDestroyFunc destroy;void *data;DrawablePtr drawable; } damage;
static struct ps2_cursor_screen screen_cursor;
static unsigned char source_bits[512],mask_bits[512];
static uint32_t background_pixels[720*576];
static struct ps2_pipe_cursor *cursor;
static unsigned pointer_closes,blocks,best_calls;
static DeviceIntRec master_device={.type=MASTER_POINTER,.id=2},other_master_device={.type=MASTER_POINTER,.id=4};
static DeviceIntRec slave_device={.type=SLAVE,.id=7,.master=&master_device};
DeviceIntPtr GetMaster(DeviceIntPtr d,int type)
{assert(type==MASTER_POINTER);return d->type==MASTER_POINTER?d:d->master;}
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
 if(++attempts== (unsigned)fail_at){errno=EIO;return -1;}
 if(reject_texture && op==PS2GS_TEXTURE_SUBMIT){errno=ENOTSUP;return -1;}
 if(op==PS2GS_SOLID_LOGIC){
  const struct ps2gs_solid_logic *r=arg;struct pixels *p=model(r->handle);assert(r->alu==GXcopy);
  for(unsigned y=r->y;y<r->y+r->height;++y)for(unsigned x=r->x;x<r->x+r->width;++x){
   assert(x<p->width && y<p->height);uint32_t *v=p->data+y*p->width+x;*v=(*v&~r->plane_mask)|(r->color&r->plane_mask);
  }++cursor_solids;return 0;
 }
 if(op==PS2GS_SURFACE_UPLOAD){
  const struct ps2gs_surface_upload *r=arg;struct pixels *p=model(r->handle);const uint32_t *v=(void*)(uintptr_t)r->pixels;
  for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x)p->data[(r->y+y)*p->width+r->x+x]=v[y*r->width+x];
  return 0;
 }
 int result=copy_ioctl(fd,op,arg);if(result)return result;
 if(op==PS2GS_SURFACE_FILL){
  const struct ps2gs_surface_fill *r=arg;struct pixels *p=model(r->handle);
  for(unsigned y=r->y;y<r->y+r->height;++y)for(unsigned x=r->x;x<r->x+r->width;++x)p->data[y*p->width+x]=r->rgba;
 }else if(op==PS2GS_SURFACE_UPLOAD){
  const struct ps2gs_surface_upload *r=arg;struct pixels *p=model(r->handle);
  const uint32_t *v=(void*)(uintptr_t)r->pixels;
  for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x)p->data[(r->y+y)*p->width+r->x+x]=v[y*r->width+x];
 }else if(op==PS2GS_RENDER_SUBMIT){
  const struct ps2gs_render_submit *r=arg;struct pixels *p=model(r->target);const struct ps2gs_command *c=(void*)(uintptr_t)r->commands;
  for(unsigned i=0;i<r->count;++i)if(c[i].op==PS2GS_FILL)
   for(unsigned y=c[i].v[0].y/16;y<c[i].v[0].y/16+c[i].height;++y)
    for(unsigned x=c[i].v[0].x/16;x<c[i].v[0].x/16+c[i].width;++x)p->data[y*p->width+x]=c[i].v[0].rgba;
 }
 return 0;
}
DamagePtr DamageCreate(DamageReportFunc report,DamageDestroyFunc destroy,DamageReportLevel level,Bool internal,ScreenPtr s,void *data)
{assert(level==DamageReportRawRegion && internal && s);if(fail_damage)return NULL;damage.report=report;damage.destroy=destroy;damage.data=data;return (DamagePtr)&damage;}
void DamageSetReportAfterOp(DamagePtr d,Bool after){assert(d==(DamagePtr)&damage && !after);}
void DamageRegister(DrawablePtr drawable,DamagePtr d){assert(d==(DamagePtr)&damage);damage.drawable=drawable;}
void DamageUnregister(DamagePtr d){assert(d==(DamagePtr)&damage);damage.drawable=NULL;}
void DamageDestroy(DamagePtr d){assert(d==(DamagePtr)&damage);damage.destroy(d,damage.data);damage.report=NULL;}
void DamageEmpty(DamagePtr d){assert(d==(DamagePtr)&damage);}
static Bool pointer_close(ScreenPtr s){++pointer_closes;s->CloseScreen=pointer_lower;return s->CloseScreen(s);}
Bool miPointerInitialize(ScreenPtr s,miPointerSpriteFuncPtr funcs,miPointerScreenFuncPtr screens,Bool wait)
{assert(screens && wait);if(fail_pointer)return FALSE;sprites=funcs;pointer_lower=s->CloseScreen;s->CloseScreen=pointer_close;return TRUE;}
static void lower_block(ScreenPtr s,void *timeout){assert(s && timeout);++blocks;}
static void replacement_block(ScreenPtr s,void *timeout){lower_block(s,timeout);}
static void changing_block(ScreenPtr s,void *timeout){lower_block(s,timeout);s->BlockHandler=replacement_block;}
static void lower_best(int type,unsigned short *w,unsigned short *h,ScreenPtr s){assert(type!=CursorShape && w && h && s);++best_calls;}
static Bool offscreen(ScreenPtr *s,int *x,int *y){(void)s;(void)x;(void)y;return FALSE;}
static void crossing(ScreenPtr s,int entering){(void)s;(void)entering;}
static void warp(DeviceIntPtr d,ScreenPtr s,int x,int y){(void)d;(void)s;(void)x;(void)y;}
static const miPointerScreenFuncRec screen_funcs={offscreen,crossing,warp};
static void cursor_setup(unsigned bpp)
{
 gc_setup(1);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
 gs->front.bits_per_pixel=gs->caps.bits_per_pixel=bpp;
 xp.drawable.width=720;xp.drawable.height=576;xp.drawable.depth=bpp==16?15:24;xp.drawable.bitsPerPixel=bpp;
 xp.devPrivate.ptr=NULL;*front_pixmap=xp;xs.rootDepth=xp.drawable.depth;xs.BlockHandler=lower_block;xs.QueryBestSize=lower_best;
 seed(model(0));if(bpp==16)for(unsigned i=0;i<720*576;++i)model(0)->data[i]=(model(0)->data[i]&0x7fff)|0x8000;
 memcpy(background_pixels,model(0)->data,sizeof(background_pixels));
 for(unsigned y=0;y<64;++y)for(unsigned x=0;x<8;++x){source_bits[y*8+x]=(unsigned char)(x*37+y*19);mask_bits[y*8+x]=(unsigned char)(x*13+y*41);}
}
static struct ps2_cursor_image image(unsigned bpp,unsigned msb)
{return (struct ps2_cursor_image){source_bits,mask_bits,512,512,8,31,27,7,9,msb,bpp==16?0x7fff:0xffffff,bpp==16?0x001f:0x0000ff};}
static void golden(const struct ps2_cursor_image *im,int x,int y,int displayed)
{
 struct pixels *p=model(0);uint32_t bits=p->data[0]&0x80000000?0xffffff:0x7fff;
 for(int j=0;j<576;++j)for(int i=0;i<720;++i){
  uint32_t expected=background_pixels[j*720+i];int64_t a=(int64_t)i-x+im->xhot,b=(int64_t)j-y+im->yhot;
  if(displayed && a>=0 && b>=0 && a<(int)im->width && b<(int)im->height){
   unsigned shift=im->msb_first?7-a%8:a%8;
   if((im->mask[b*im->stride+a/8]>>shift)&1){uint32_t colour=((im->source[b*im->stride+a/8]>>shift)&1)?im->foreground:im->background;expected=(expected&~bits)|(colour&bits);}
  }
  assert(expected==p->data[j*720+i]);
 }
}
static void finish(void){if(cursor){assert(!ps2_pipe_cursor_destroy(cursor));cursor=NULL;}gc_cleanup();}
static void shape_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);
 int positions[][2]={{80,100},{0,0},{719,575},{-20,12},{730,580},{INT_MAX,INT_MIN},{180,90}};
 for(unsigned bitorder=0;bitorder<2;++bitorder){struct ps2_cursor_image im=image(32,bitorder);
  for(unsigned i=0;i<7;++i){assert(!ps2_pipe_cursor_set(cursor,&im,positions[i][0],positions[i][1]));golden(&im,positions[i][0],positions[i][1],1);}}
 assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));struct ps2_cursor_image im=image(32,0);golden(&im,0,0,0);finish();
}
static void snapshot_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);
 unsigned char source[512],mask[512];memcpy(source,source_bits,512);memcpy(mask,mask_bits,512);
 assert(!ps2_pipe_cursor_set(cursor,&im,90,80));memset(source_bits,0,512);memset(mask_bits,0,512);
 assert(!ps2_pipe_cursor_move(cursor,120,130));im.source=source;im.mask=mask;golden(&im,120,130,1);finish();
}
static void repeated_case(void)
{
 /* Save-under must be bit-exact without texture sampling, at every x
  * residue and at clipped edges, for both GS framebuffer formats. */
 reject_texture=1;
 for(unsigned bpp=16;bpp<=32;bpp+=16){
  cursor_setup(bpp);ps2_pipe_context_render(ctx)->gs->caps.features&=~PS2GS_HW_TEXTURE;
  cursor=ps2_pipe_cursor_create(ctx);assert(cursor);
  struct ps2_cursor_image im=image(bpp,0);im.width=64;im.height=64;
  assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
  for(unsigned i=0;i<256;++i){
   int x=(i*37)%760-20,y=(i*53)%616-20;
   assert(!ps2_pipe_cursor_move(cursor,x,y));golden(&im,x,y,1);
  }
  assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));golden(&im,0,0,0);finish();
 }
}
static void guards_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
 assert(!ps2_surface_fill(gs,0,72,62,48,48,0x8099aabb));for(int y=62;y<110;++y)for(int x=72;x<120;++x)background_pixels[y*720+x]=0x8099aabb;
 golden(&im,90,80,0);assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);
 struct ps2_pipe_solid_run run={.box={80,70,110,95},.color=0xabcdef};assert(!ps2_pipe_solid_runs(ctx,0,&run,1,0xffffff,GXcopy));
 for(int y=70;y<95;++y)for(int x=80;x<110;++x)background_pixels[y*720+x]=(background_pixels[y*720+x]&0xff000000)|0xabcdef;
 assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);
 uint32_t upload[4]={0x803355aa,0x80abcdef,0x80112233,0x80aabbcc};assert(!ps2_surface_upload(gs,0,89,79,2,2,upload,sizeof(upload),8));
 for(int y=0;y<2;++y)for(int x=0;x<2;++x)background_pixels[(79+y)*720+89+x]=upload[y*2+x];
 assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);finish();
}
static void copy_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;uint32_t target;assert(!ps2_surface_create(gs,64,64,32,&target));
 assert(!ps2_surface_copy(gs,0,target,64,48,0,0,64,64));struct pixels *p=model(target);
 for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)assert(p->data[y*64+x]==background_pixels[(48+y)*720+64+x]);
 golden(&im,90,80,0);assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);
 assert(!ps2_surface_copy(gs,target,0,0,0,200,200,64,64));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)background_pixels[(200+y)*720+200+x]=p->data[y*64+x];
 assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);struct ps2_pipe_copy_box tail={0,0,89,79,1,3};
 assert(!ps2_pipe_copy_boxes(ctx,target,0,&tail,1));
 for(unsigned y=0;y<3;++y)background_pixels[(79+y)*720+89]=p->data[y*64];
 assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);
 assert(!ps2_surface_destroy(gs,target));finish();
}
static void queued_case(void)
{
 cursor_setup(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_bind(r,0,0));
 cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 unsigned before=render_calls;assert(!ps2_render_fill(r,60,50,64,64,0x80445566));assert(!ps2_pipe_cursor_flush(cursor));assert(render_calls==before+1 && !r->count);
 for(int y=50;y<114;++y)for(int x=60;x<124;++x)background_pixels[y*720+x]=0x80445566;
 golden(&im,90,80,1);finish();
}
static void rgb555_case(void)
{
 cursor_setup(16);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(16,1);
 assert(!ps2_pipe_cursor_set(cursor,&im,719,575));golden(&im,719,575,1);
 assert(!ps2_pipe_cursor_move(cursor,0,0));golden(&im,0,0,1);finish();
}
static void preflight_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=attempts,queued=r->count;
 for(unsigned i=0;i<8;++i){struct ps2_cursor_image bad=im;
  if(i==0)bad.width=65;
  if(i==1)bad.height=0;
  if(i==2)bad.xhot=31;
  if(i==3)bad.stride=1;
  if(i==4)bad.mask_bytes=1;
  if(i==5)bad.msb_first=2;
  if(i==6)bad.foreground=0x1000000;
  if(i==7)bad.stride=SIZE_MAX;
  assert(ps2_pipe_cursor_set(cursor,&bad,90,80)<0 && errno==EINVAL && attempts==before && r->count==queued);golden(&im,90,80,1);
 }finish();
}
static void budget_case(void)
{
 cursor_setup(32);memset(mask_bits,255,512);memset(source_bits,0x55,512);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);
 struct ps2_cursor_image im=image(32,0);im.width=im.height=64;im.xhot=im.yhot=0;unsigned before=cursor_solids;
 assert(!ps2_pipe_cursor_set(cursor,&im,100,100));assert(cursor_solids==before+4096);golden(&im,100,100,1);
 memset(mask_bits,0,512);assert(!ps2_pipe_cursor_set(cursor,&im,120,120));golden(&im,120,120,1);finish();
}
static void suspend_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 assert(!ps2_pipe_cursor_suspend(cursor,1));golden(&im,90,80,0);assert(!ps2_pipe_cursor_move(cursor,150,120));golden(&im,150,120,0);
 assert(!ps2_pipe_cursor_flush(cursor));golden(&im,150,120,0);assert(!ps2_pipe_cursor_suspend(cursor,0));golden(&im,150,120,1);finish();
}
static int other_observer(void *data){assert(data);return 0;}
static void ownership_case(void)
{
 cursor_setup(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;gs->front_access=other_observer;gs->front_access_data=gs;
 unsigned before=attempts;assert(!ps2_pipe_cursor_create(ctx) && attempts==before);gs->front_access=NULL;cursor=ps2_pipe_cursor_create(ctx);assert(cursor);
 int (*saved)(void*)=gs->front_access;gs->front_access=other_observer;assert(ps2_pipe_cursor_destroy(cursor)<0 && errno==EBUSY);gs->front_access=saved;finish();
}
static void cursor_lifetime_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);ctx->destroy(ctx);struct ps2_cursor_image im=image(32,0);
 assert(ps2_pipe_cursor_set(cursor,&im,0,0)<0 && errno==ESHUTDOWN);assert(!ps2_pipe_cursor_destroy(cursor));cursor=NULL;gc_cleanup();assert(closes==1);
}
static void cursor_failure_case(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);
 fail_at=attempts+2;assert(ps2_pipe_cursor_set(cursor,&im,90,80)<0 && errno==EIO);unsigned before=attempts;
 assert(ps2_pipe_cursor_flush(cursor)<0 && ps2_pipe_cursor_move(cursor,100,100)<0 && attempts==before);
 assert(!ps2_pipe_cursor_destroy(cursor));cursor=NULL;gc_cleanup();assert(closes==1);
}
static CursorRec xcursor(void)
{
 static CursorBits bits;bits=(CursorBits){.source=source_bits,.mask=mask_bits,.width=64,.height=27,.xhot=7,.yhot=9};
 return (CursorRec){.bits=&bits,.foreRed=65535,.foreGreen=65535,.foreBlue=65535,.backRed=65535};
}
static void screen_setup(void)
{cursor_setup(32);assert(ps2_cursor_screen_prepare(&xs,&screen_cursor,ctx,(miPointerScreenFuncPtr)&screen_funcs));}
static void screen_finish(void){assert(!xs.CloseScreen(&xs));assert(!screen_cursor.installed && !damage.report && pointer_closes==1);assert(!memcmp(background_pixels,model(0)->data,sizeof(background_pixels)));gc_cleanup();}
static void pointer_case(void)
{
 screen_setup();DeviceIntPtr d=&master_device;assert(sprites->DeviceCursorInitialize(d,&xs));assert(!sprites->DeviceCursorInitialize(&other_master_device,&xs));
 CursorRec c=xcursor();assert(sprites->RealizeCursor(d,&xs,&c));sprites->SetCursor(d,&xs,&c,90,80);
 struct ps2_cursor_image im=image(32,BITMAP_BIT_ORDER==MSBFirst);im.width=64;golden(&im,90,80,1);sprites->MoveCursor(d,&xs,110,120);golden(&im,110,120,1);
 c.foreRed=0;c.foreBlue=65535;sprites->SetCursor(d,&xs,&c,110,120);im.foreground=0xffff00;golden(&im,110,120,1);
 CARD32 argb=0;c.bits->argb=&argb;assert(sprites->RealizeCursor(d,&xs,&c));c.bits->argb=NULL;
 assert(sprites->UnrealizeCursor(d,&xs,&c));golden(&im,110,120,0);sprites->DeviceCursorCleanup(d,&xs);screen_finish();
}
static void damage_case(void)
{
 screen_setup();DeviceIntPtr d=&master_device;assert(sprites->DeviceCursorInitialize(d,&xs));CursorRec c=xcursor();sprites->SetCursor(d,&xs,&c,90,80);
 struct ps2_cursor_image im=image(32,BITMAP_BIT_ORDER==MSBFirst);im.width=64;damage.report((DamagePtr)&damage,NULL,damage.data);golden(&im,90,80,0);
 int timeout=0;xs.BlockHandler(&xs,&timeout);golden(&im,90,80,1);assert(blocks==1);screen_finish();
}
static void close_case(void)
{
 cursor_setup(32);xs.BlockHandler=changing_block;assert(ps2_cursor_screen_prepare(&xs,&screen_cursor,ctx,(miPointerScreenFuncPtr)&screen_funcs));
 unsigned short w=100,h=88;xs.QueryBestSize(CursorShape,&w,&h,&xs);assert(w==64 && h==64);xs.QueryBestSize(TileShape,&w,&h,&xs);assert(best_calls==1);
 int timeout=0;xs.BlockHandler(&xs,&timeout);assert(blocks==1);DeviceIntPtr d=&master_device;assert(sprites->DeviceCursorInitialize(d,&xs));CursorRec c=xcursor();sprites->SetCursor(d,&xs,&c,90,80);
 assert(ps2_cursor_screen_suspend(&screen_cursor,TRUE));struct ps2_cursor_image im=image(32,BITMAP_BIT_ORDER==MSBFirst);im.width=64;golden(&im,90,80,0);
 assert(ps2_cursor_screen_suspend(&screen_cursor,FALSE));golden(&im,90,80,1);
 fail_at=attempts+1;assert(!xs.CloseScreen(&xs));
 assert(!screen_cursor.installed && !screen_cursor.native && !damage.report && pointer_closes==1);
 assert(!ps2_pipe_context_render(ctx)->gs->front_access);
 assert(xs.BlockHandler==replacement_block && xs.QueryBestSize==lower_best);
 gc_cleanup();assert(closes==1);
}
static void installation_case(void)
{
 cursor_setup(32);unsigned before=attempts;xp.devPrivate.ptr=(void*)(uintptr_t)1;assert(!ps2_cursor_screen_prepare(&xs,&screen_cursor,ctx,(miPointerScreenFuncPtr)&screen_funcs) && attempts==before);xp.devPrivate.ptr=NULL;
 allocation_failure=1;assert(!ps2_pipe_cursor_create(ctx));allocation_failure=0;
 fail_damage=1;assert(!ps2_cursor_screen_prepare(&xs,&screen_cursor,ctx,(miPointerScreenFuncPtr)&screen_funcs));fail_damage=0;
 assert(!ps2_pipe_context_render(ctx)->gs->front_access && !screen_cursor.installed);
 fail_pointer=1;assert(!ps2_cursor_screen_prepare(&xs,&screen_cursor,ctx,(miPointerScreenFuncPtr)&screen_funcs));fail_pointer=0;
 assert(!ps2_pipe_context_render(ctx)->gs->front_access && !screen_cursor.installed && !damage.report);
 assert(ps2_cursor_screen_prepare(&xs,&screen_cursor,ctx,(miPointerScreenFuncPtr)&screen_funcs));screen_finish();
}
static void empty_case(void)
{
 screen_setup();DeviceIntPtr d=&master_device;assert(sprites->DeviceCursorInitialize(d,&xs));
 CursorRec visible=xcursor();sprites->SetCursor(d,&xs,&visible,90,80);
 struct ps2_cursor_image im=image(32,BITMAP_BIT_ORDER==MSBFirst);im.width=64;golden(&im,90,80,1);
 CursorBits empty_bits={.emptyMask=TRUE};CursorRec empty={.bits=&empty_bits};
 assert(sprites->RealizeCursor(d,&xs,&empty));sprites->SetCursor(d,&xs,&empty,90,80);
 assert(screen_cursor.current==&empty);golden(&im,90,80,0);
 unsigned before=attempts;sprites->MoveCursor(&slave_device,&xs,110,120);
 int timeout=0;xs.BlockHandler(&xs,&timeout);assert(attempts==before);golden(&im,110,120,0);
 /* Invisible font glyphs need no shape allocation or valid visible hotspot. */
 empty_bits.width=96;empty_bits.height=13;empty_bits.xhot=96;
 assert(sprites->RealizeCursor(d,&xs,&empty));sprites->SetCursor(d,&xs,&empty,140,150);
 assert(attempts==before);golden(&im,140,150,0);
 CARD32 argb=0;empty_bits.argb=&argb;assert(!sprites->RealizeCursor(d,&xs,&empty));empty_bits.argb=NULL;
 sprites->SetCursor(d,&xs,&visible,170,180);golden(&im,170,180,1);
 assert(sprites->UnrealizeCursor(d,&xs,&empty));assert(screen_cursor.current==&visible);golden(&im,170,180,1);
 sprites->SetCursor(d,&xs,&empty,170,180);assert(sprites->UnrealizeCursor(d,&xs,&empty));
 assert(!screen_cursor.current);golden(&im,170,180,0);screen_finish();
}
static void slave_case(void)
{
 screen_setup();assert(sprites->DeviceCursorInitialize(&master_device,&xs));
 assert(sprites->DeviceCursorInitialize(&slave_device,&xs));
 CursorRec c=xcursor();sprites->SetCursor(&master_device,&xs,&c,90,80);
 struct ps2_cursor_image im=image(32,BITMAP_BIT_ORDER==MSBFirst);im.width=64;
 sprites->MoveCursor(&slave_device,&xs,140,150);golden(&im,140,150,1);
 sprites->SetCursor(&slave_device,&xs,&c,170,180);golden(&im,170,180,1);
 sprites->DeviceCursorCleanup(&slave_device,&xs);
 assert(screen_cursor.device==&master_device && screen_cursor.current==&c);golden(&im,170,180,1);
 sprites->MoveCursor(&master_device,&xs,200,190);golden(&im,200,190,1);
 unsigned before=attempts;expect_fatal=1;
 if(!setjmp(fatal)){sprites->MoveCursor(&other_master_device,&xs,300,300);assert(0);}
 expect_fatal=0;assert(attempts==before);golden(&im,200,190,1);
 sprites->DeviceCursorCleanup(&master_device,&xs);golden(&im,200,190,0);screen_finish();
}
static void unrelated_case(void)
{
 screen_setup();assert(sprites->DeviceCursorInitialize(&master_device,&xs));
 CursorRec c=xcursor();sprites->SetCursor(&master_device,&xs,&c,90,80);
 struct ps2_cursor_image im=image(32,BITMAP_BIT_ORDER==MSBFirst);im.width=64;
 unsigned before=attempts;
 BoxRec box={300,300,320,320};RegionRec region;RegionInit(&region,&box,1);
 damage.report((DamagePtr)&damage,&region,damage.data);
 assert(attempts==before);golden(&im,90,80,1);RegionUninit(&region);
 struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
 assert(!ps2_surface_fill(gs,0,300,300,8,8,0x80abcdef));assert(attempts==before+1);
 for(unsigned y=300;y<308;++y)for(unsigned x=300;x<308;++x)background_pixels[y*720+x]=0x80abcdef;
 golden(&im,90,80,1);
 before=attempts;uint32_t target;assert(!ps2_surface_create(gs,64,64,32,&target));
 assert(!ps2_surface_copy(gs,0,target,300,300,0,0,8,8));assert(attempts==before+2);
 golden(&im,90,80,1);assert(!ps2_surface_destroy(gs,target));
 box=(BoxRec){85,75,100,100};RegionInit(&region,&box,1);
 damage.report((DamagePtr)&damage,&region,damage.data);golden(&im,90,80,0);
 RegionUninit(&region);ps2_cursor_screen_refresh(&xs);golden(&im,90,80,1);screen_finish();
}
int main(int argc,char **argv)
{
 assert(argc==2);
 if(!strcmp(argv[1],"shape"))shape_case();else if(!strcmp(argv[1],"snapshot"))snapshot_case();
 else if(!strcmp(argv[1],"guards"))guards_case();else if(!strcmp(argv[1],"copy"))copy_case();
 else if(!strcmp(argv[1],"queued"))queued_case();else if(!strcmp(argv[1],"rgb555"))rgb555_case();
 else if(!strcmp(argv[1],"preflight"))preflight_case();else if(!strcmp(argv[1],"budget"))budget_case();
 else if(!strcmp(argv[1],"suspend"))suspend_case();else if(!strcmp(argv[1],"ownership"))ownership_case();
 else if(!strcmp(argv[1],"lifetime"))cursor_lifetime_case();else if(!strcmp(argv[1],"failure"))cursor_failure_case();
 else if(!strcmp(argv[1],"pointer"))pointer_case();else if(!strcmp(argv[1],"damage"))damage_case();
 else if(!strcmp(argv[1],"close"))close_case();else if(!strcmp(argv[1],"installation"))installation_case();
 else if(!strcmp(argv[1],"slave"))slave_case();else if(!strcmp(argv[1],"empty"))empty_case();
 else if(!strcmp(argv[1],"unrelated"))unrelated_case();
 else if(!strcmp(argv[1],"repeated"))repeated_case();else assert(0);
 puts("native GS cursor case PASS");return 0;
}
