/* SPDX-License-Identifier: MIT */
#include "previous-cursor.c"
#include "ps2_image.h"
#include "ps2_image_gc.h"
#include "servermd.h"
PaddingInfo PixmapWidthPaddingInfo[33] = {
    [1]={31,5,2,0,0,1}, [4]={3,2,2,0,0,8}, [8]={3,2,2,0,0,8},
    [15]={1,1,2,0,0,16}, [16]={1,1,2,0,0,16}, [24]={0,0,2,0,0,32}, [32]={0,0,2,0,0,32}
};
static struct ps2_image_screen image_owner;
static unsigned image_uploads,image_logic,mal_calls;
static int malloc_fail,upload_fail;
static unsigned char *mutate_source;
static size_t mutate_bytes;
static struct ps2_pipe_image_box *mutate_box;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n){if(malloc_fail && ++mal_calls==(unsigned)malloc_fail){errno=ENOMEM;return NULL;}
 return __real_malloc(n);}
static uint32_t image_boolean(unsigned alu,uint32_t s,uint32_t d)
{
 switch(alu){case 0:return 0;case 1:return s&d;case 2:return s&~d;case 3:return s;
 case 4:return ~s&d;case 5:return d;case 6:return s^d;case 7:return s|d;
 case 8:return ~(s|d);case 9:return ~(s^d);case 10:return ~d;case 11:return s|~d;
 case 12:return ~s;case 13:return ~s|d;case 14:return ~(s&d);default:assert(alu==15);return ~0U;}
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
 if(op==PS2GS_RENDER_SUBMIT && mutate_source){memset(mutate_source,0,mutate_bytes);mutate_source=NULL;if(mutate_box)mutate_box->x=300;}
 if(op==PS2GS_SURFACE_UPLOAD){
  if(++image_uploads==(unsigned)upload_fail){errno=EIO;return -1;}
  const struct ps2gs_surface_upload *r=arg;struct pixels *p=model(r->handle);
  const struct ps2_surface *s=ps2_surface_lookup(ps2_pipe_context_render(ctx)->gs,r->handle);assert(s);
  unsigned cpp=s->bits_per_pixel/8;assert(r->bytes==r->width*r->height*cpp);
  const unsigned char *data=(void*)(uintptr_t)r->pixels;
  for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x){uint32_t v=0;memcpy(&v,data+(y*r->width+x)*cpp,cpp);p->data[(r->y+y)*p->width+r->x+x]=v;}
  return 0;
 }
 if(op==PS2GS_SOLID_LOGIC){
  const struct ps2gs_solid_logic *r=arg;struct pixels *p=model(r->handle);++image_logic;
  for(unsigned y=r->y;y<r->y+r->height;++y)for(unsigned x=r->x;x<r->x+r->width;++x){uint32_t *v=p->data+y*p->width+x;*v=(*v&~r->plane_mask)|(image_boolean(r->alu,r->color,*v)&r->plane_mask);}
 return 0;
 }
 return cursor_ioctl(fd,op,arg);
}
static uint32_t destination_handle(void){uint32_t h=0;if(xp.devPrivates)memcpy(&h,xp.devPrivates,4);return h;}
static void image_setup(int owned,unsigned bpp)
{
 cursor_setup(bpp);window.drawable.bitsPerPixel=bpp;window.drawable.depth=bpp==16?15:24;
 if(owned){static PixmapRec fp;fp=xp;front_pixmap=&fp;int pitch;xp.devPrivates=exa.driver.CreatePixmap2(&xs,128,96,bpp==16?15:24,0,bpp,&pitch);assert(xp.devPrivates);xp.drawable.width=128;xp.drawable.height=96;xp.screen_x=20;xp.screen_y=30;}
 pixman_region_fini(&clip_region);pixman_box16_t clips[]={{20,30,100,70},{112,30,180,70},{25,85,185,110}};assert(pixman_region_init_rects(&clip_region,clips,3));
 gc.depth=window.drawable.depth;gc.planemask=bpp==16?0x7fff:0xffffff;gc.alu=GXcopy;gc.miTranslate=TRUE;
 assert(ps2_image_screen_prepare(&xs,&image_owner,&gc_screen));
 seed(model(destination_handle()));
}
static void image_finish(void){ps2_image_screen_cleanup(&image_owner);ps2_image_screen_cleanup(&image_owner);finish();}
static void check_pixels(const uint32_t *expected,struct pixels *p,unsigned bits,int preserve_unused)
{for(unsigned i=0;i<p->width*p->height;++i){assert((expected[i]&bits)==(p->data[i]&bits));if(preserve_unused)assert(expected[i]==p->data[i]);}}
static unsigned char source_image[32768];
static uint32_t source_value(unsigned x,unsigned y){return (0x80102030U+((x/3)*0x030201))^((y/2)*0x10131);}
static size_t make_image(unsigned w,unsigned h,unsigned bpp)
{
 size_t stride=(w*(bpp/8)+3)&~3U;memset(source_image,0xa7,sizeof(source_image));
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint32_t v=source_value(x,y);memcpy(source_image+y*stride+x*(bpp/8),&v,bpp/8);}
 return stride;
}
static void checked_put(DrawablePtr d,int x,int y,unsigned w,unsigned h)
{
 struct pixels *p=model(destination_handle());size_t bytes=p->width*p->height*4;
 uint32_t *expected=malloc(bytes);assert(expected);memcpy(expected,p->data,bytes);
 unsigned bpp=d->bitsPerPixel,bits=bpp==16?0x7fff:0xffffff;size_t stride=make_image(w,h,bpp);
 int ox=d->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=d->type==DRAWABLE_WINDOW?xp.screen_y:0;unsigned changed=0;
 for(unsigned j=0;j<p->height;++j)for(unsigned i=0;i<p->width;++i){int sx=(int)i+ox-x-d->x,sy=(int)j+oy-y-d->y;
  if(sx<0 || sy<0 || sx>=(int)w || sy>=(int)h || !pixman_region_contains_point(&clip_region,i+ox,j+oy,NULL))continue;
  uint32_t v=0;memcpy(&v,source_image+sy*stride+sx*(bpp/8),bpp/8);uint32_t *q=expected+j*p->width+i;
  *q=(*q&~(gc.planemask&bits))|(image_boolean(gc.alu,v,*q)&gc.planemask&bits);++changed;}
 unsigned sync=sync_calls,damage_before=damage_calls;unsigned char unchanged[32768];memcpy(unchanged,source_image,sizeof(source_image));gc.ops->PutImage(d,&gc,d->depth,x,y,w,h,0,ZPixmap,(char*)source_image);
 assert(!memcmp(unchanged,source_image,sizeof(source_image)));check_pixels(expected,p,bits,gc.alu!=GXcopy || (gc.planemask&bits)!=bits);
 int emitted=changed && (gc.planemask&bits) && gc.alu!=GXnoop;
 assert(sync_calls==sync+(unsigned)emitted && damage_calls==damage_before+(unsigned)emitted);
 free(expected);
}
static void put_case(void){image_setup(0,32);checked_put(&window.drawable,-7,-3,39,27);gc.miTranslate=FALSE;checked_put(&window.drawable,0,0,37,21);assert(image_uploads && !image_logic);image_finish();}
static void span_check(unsigned bpp)
{
 DDXPointRec points[]={{17,33},{24,34},{24,34},{85,55}};int widths[]={31,19,19,17};
 size_t total=0;for(unsigned i=0;i<4;++i){size_t row=(widths[i]*(bpp/8)+3)&~3U;for(int x=0;x<widths[i];++x){uint32_t v=source_value(x,i);memcpy(source_image+total+x*(bpp/8),&v,bpp/8);}total+=row;}
 struct pixels *p=model(destination_handle());size_t bytes=p->width*p->height*4;uint32_t *expected=malloc(bytes);assert(expected);memcpy(expected,p->data,bytes);
 unsigned bits=bpp==16?0x7fff:0xffffff;size_t offset=0;
 for(unsigned i=0;i<4;++i){for(int x=0;x<widths[i];++x){int sx=points[i].x+x,sy=points[i].y;
  int dx=sx-xp.screen_x,dy=sy-xp.screen_y;if(dx<0 || dy<0 || dx>=(int)p->width || dy>=(int)p->height || !pixman_region_contains_point(&clip_region,sx,sy,NULL))continue;
  uint32_t v=0;memcpy(&v,source_image+offset+x*(bpp/8),bpp/8);uint32_t *q=expected+dy*p->width+dx;
  *q=(*q&~(gc.planemask&bits))|(image_boolean(gc.alu,v,*q)&gc.planemask&bits);}
  offset+=(widths[i]*(bpp/8)+3)&~3U;}
 gc.ops->SetSpans(&window.drawable,&gc,(char*)source_image,points,widths,4,0);
 check_pixels(expected,p,bits,gc.alu!=GXcopy || (gc.planemask&bits)!=bits);free(expected);
}
static void spans_case(void){image_setup(0,32);for(unsigned translate=0;translate<2;++translate){gc.miTranslate=translate;gc.alu=GXcopy;span_check(32);gc.alu=GXxor;span_check(32);}image_finish();}
static void redirected_case(void){image_setup(1,32);checked_put(&window.drawable,-5,-2,41,29);xp.drawable.x=7;xp.drawable.y=9;checked_put(&xp.drawable,12,23,31,17);span_check(32);image_finish();}
static void image_rgb555_case(void){image_setup(1,16);checked_put(&window.drawable,-2,-1,37,19);span_check(16);gc.alu=GXxor;gc.planemask=0x513f;checked_put(&window.drawable,0,0,33,17);span_check(16);image_finish();}
static void boolean_case(void){image_setup(1,32);for(unsigned alu=0;alu<16;++alu){gc.alu=alu;gc.planemask=0xf13f77;checked_put(&window.drawable,1,2,21,13);span_check(32);}
 assert(image_logic);image_finish();}
static void snapshot_image_case(void)
{
 image_setup(0,32);make_image(19,7,32);unsigned char copy[19*7*4];memcpy(copy,source_image,sizeof(copy));
 struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_bind(r,0,0));assert(!ps2_render_fill(r,0,0,2,2,0x80444444));
 struct ps2_pipe_image_box box={0,76,70,60,19,7};mutate_source=source_image;mutate_bytes=sizeof(copy);mutate_box=&box;
 assert(!ps2_pipe_image_boxes(ctx,0,32,source_image,sizeof(copy),&box,1,0xffffff,GXcopy));
 for(unsigned y=0;y<7;++y)for(unsigned x=0;x<19;++x){uint32_t v;memcpy(&v,copy+(y*19+x)*4,4);assert(model(0)->data[(60+y)*720+70+x]==v);}
 assert(!r->count && box.x==300);mutate_box=NULL;image_finish();
}
static void image_noop_case(void)
{
 image_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 gc.alu=GXnoop;checked_put(&window.drawable,0,0,19,7);gc.alu=GXcopy;gc.planemask=0;checked_put(&window.drawable,0,0,19,7);gc.planemask=0xffffff;checked_put(&window.drawable,400,400,19,7);
 assert(attempts==before && r->count==1 && !image_uploads && !image_logic);image_finish();
}
static void reject_put(int depth,int w,int h,int pad,int format)
{expect_fatal=1;if(!setjmp(fatal)){gc.ops->PutImage(&window.drawable,&gc,depth,0,0,w,h,pad,format,(char*)source_image);assert(0);}
 assert(!expect_fatal);}
static void image_preflight_case(void)
{
 image_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 reject_put(24,19,7,1,ZPixmap);reject_put(1,19,7,0,XYPixmap);reject_put(24,19,7,0,XYBitmap);reject_put(24,-1,7,0,ZPixmap);reject_put(24,2049,7,0,ZPixmap);
 struct ps2_pipe_image_box boxes[]={{0,8,0,0,2,2},{100,8,0,0,2,2}};unsigned char data[16]={0};assert(ps2_pipe_image_boxes(ctx,0,32,data,16,boxes,2,0xffffff,3)==-1);
 boxes[1]=boxes[0];boxes[1].stride=0;assert(ps2_pipe_image_boxes(ctx,0,32,data,16,boxes,2,0xffffff,3)==-1);
 boxes[1].offset=SIZE_MAX;assert(ps2_pipe_image_boxes(ctx,0,32,data,16,boxes,2,0xffffff,3)==-1);
 assert(attempts==before && r->count==1 && !image_uploads);image_finish();
}
static void allocation_case(void)
{
 image_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 unsigned char data[16]={0};struct ps2_pipe_image_box box={0,8,0,0,2,2};
 for(int i=1;i<=3;++i){malloc_fail=i;mal_calls=0;assert(ps2_pipe_image_boxes(ctx,0,32,data,16,&box,1,0x123456,GXxor)==-1 && errno==ENOMEM);malloc_fail=0;}
 allocation_failure=1;reject_put(24,19,7,0,ZPixmap);allocation_failure=0;
 assert(attempts==before && r->count==1 && !image_uploads && !image_logic);image_finish();
}
static void image_budget_case(void)
{
 image_setup(0,32);unsigned w=129,h=129;uint32_t *data=malloc(w*h*4);assert(data);for(unsigned i=0;i<w*h;++i)data[i]=i;
 struct ps2_pipe_image_box box={0,w*4,0,0,w,h};struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 assert(ps2_pipe_image_boxes(ctx,0,32,data,w*h*4,&box,1,0x123456,GXxor)==-1 && errno==E2BIG);assert(attempts==before && r->count==1);free(data);image_finish();
}
static void image_failure_case(void)
{
 image_setup(0,32);unsigned char data[4096]={0};struct ps2_pipe_image_box box={0,256,0,0,64,16};upload_fail=2;
 expect_fatal=1;if(!setjmp(fatal)){gc.ops->PutImage(&window.drawable,&gc,24,0,0,64,16,0,ZPixmap,(char*)data);assert(0);}
 assert(image_uploads==2 && !damage_calls && ps2_pipe_context_error(ctx)==EIO);
 unsigned before=image_uploads;assert(ps2_pipe_image_boxes(ctx,0,32,data,4096,&box,1,0xffffff,3)==-1 && image_uploads==before);image_finish();
}
static void image_rewrap_case(void)
{image_setup(0,32);const GCOps *ops=gc.ops;gc.funcs->ChangeGC(&gc,GCForeground);gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);assert(gc.ops==ops);checked_put(&window.drawable,0,0,19,7);ps2_image_screen_cleanup(&image_owner);assert(!gc_screen.image_ops);assert(ps2_image_screen_prepare(&xs,&image_owner,&gc_screen));checked_put(&window.drawable,0,0,19,7);image_finish();}
static void image_lifetime_case(void)
{image_setup(0,32);ctx->destroy(ctx);expect_fatal=1;if(!setjmp(fatal)){gc.ops->PutImage(&window.drawable,&gc,24,0,0,1,1,0,ZPixmap,(char*)source_image);assert(0);}ps2_image_screen_cleanup(&image_owner);ps2_gc_screen_cleanup(&gc_screen);release_external();screen->destroy(screen);assert(closes==1);}
static void image_installation_case(void)
{image_setup(0,32);struct ps2_image_screen other={0};assert(!ps2_image_screen_prepare(&xs,&other,&gc_screen));GCOps incomplete={.PutImage=gc.ops->PutImage};assert(!ps2_gc_screen_set_image_ops(&gc_screen,&incomplete));ps2_image_screen_cleanup(&image_owner);assert(!gc_screen.image_ops);assert(ps2_image_screen_prepare(&xs,&image_owner,&gc_screen));assert(!xs.CloseScreen(&xs));assert(!image_owner.installed && !gc_screen.installed);gc_cleanup();}
static void image_cursor_case(void)
{
 image_setup(0,32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 size_t stride=make_image(48,48,32);struct ps2_pipe_image_box box={0,stride,64,64,48,48};
 assert(!ps2_pipe_image_boxes(ctx,0,32,source_image,48*stride,&box,1,0xffffff,GXcopy));
 for(unsigned y=0;y<48;++y)for(unsigned x=0;x<48;++x){uint32_t v;memcpy(&v,source_image+y*stride+x*4,4);background_pixels[(64+y)*720+64+x]=v;}
 golden(&im,90,80,0);assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));golden(&im,90,80,0);image_finish();
}
static void image_random_case(void)
{image_setup(1,32);uint32_t state=123;for(unsigned i=0;i<40;++i){state=state*1664525U+1013904223U;gc.alu=state%16;gc.planemask=state&0xffffff;checked_put(&window.drawable,(state>>8)%33-12,(state>>16)%21-9,(state>>3)%45+1,(state>>24)%23+1);}image_finish();}
int main(int argc,char **argv)
{
 assert(argc==2);
 if(!strcmp(argv[1],"put"))put_case();else if(!strcmp(argv[1],"spans"))spans_case();else if(!strcmp(argv[1],"redirected"))redirected_case();else if(!strcmp(argv[1],"rgb555"))image_rgb555_case();else if(!strcmp(argv[1],"boolean"))boolean_case();else if(!strcmp(argv[1],"snapshot"))snapshot_image_case();else if(!strcmp(argv[1],"noop"))image_noop_case();else if(!strcmp(argv[1],"preflight"))image_preflight_case();else if(!strcmp(argv[1],"allocation"))allocation_case();else if(!strcmp(argv[1],"budget"))image_budget_case();else if(!strcmp(argv[1],"failure"))image_failure_case();else if(!strcmp(argv[1],"rewrap"))image_rewrap_case();else if(!strcmp(argv[1],"lifetime"))image_lifetime_case();else if(!strcmp(argv[1],"installation"))image_installation_case();else if(!strcmp(argv[1],"cursor"))image_cursor_case();else if(!strcmp(argv[1],"random"))image_random_case();else assert(0);
 puts("native client image case PASS");return 0;
}
