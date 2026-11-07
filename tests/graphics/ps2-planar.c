/* SPDX-License-Identifier: MIT */
#include "previous-image.c"
#include "ps2_planar.h"
static unsigned char xy_bits[262144];
static unsigned xy_calls,xy_fills,xy_requests;
static int xy_fail;
static struct ps2_pipe_planar_box *xy_mutate_box;
static struct ps2_pipe_planar_source *xy_mutate_header;
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
 if(op==PS2GS_RENDER_SUBMIT && xy_mutate_box){xy_mutate_box->x=600;xy_mutate_box=NULL;
  xy_mutate_header->depth=99;xy_mutate_header->bits=NULL;xy_mutate_header=NULL;}
 if(op==PS2GS_SOLID_LOGIC || op==PS2GS_SURFACE_FILL){
  if(op==PS2GS_SOLID_LOGIC)++xy_calls;else ++xy_fills;
  if(++xy_requests==(unsigned)xy_fail){errno=EIO;return -1;}
  if(op==PS2GS_SURFACE_FILL){
   const struct ps2gs_surface_fill *q=arg;const struct ps2_surface *target=ps2_surface_lookup(ps2_pipe_context_render(ctx)->gs,q->handle);
   if(target->bits_per_pixel==16){struct pixels *p=model(q->handle);
    uint32_t c=((q->rgba>>3)&31)|(((q->rgba>>11)&31)<<5)|(((q->rgba>>19)&31)<<10)|((q->rgba>>16)&0x8000);
    for(unsigned y=q->y;y<q->y+q->height;++y)for(unsigned x=q->x;x<q->x+q->width;++x)p->data[y*p->width+x]=c;
    return 0;
   }
  }
 }
 return image_ioctl(fd,op,arg);
}
static uint32_t xy_colour(unsigned x,unsigned y,unsigned depth)
{
 if(depth==1)return ((x/5+y/3)&1)?gc.fgPixel:gc.bgPixel;
 return (x/5%3==0?0x2047e3:x/5%3==1?0xca1531:0x65b74e)^((y/3)*0x15101);
}
static struct ps2_pipe_planar_source make_xy(unsigned w,unsigned h,unsigned pad,unsigned depth,unsigned msb)
{
 size_t stride=((w+pad+31)/32)*4,plane_stride=stride*h,bytes=plane_stride*depth;
 assert(bytes<=sizeof(xy_bits));memset(xy_bits,0xa7,sizeof(xy_bits));
 for(unsigned p=0;p<depth;++p)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
  unsigned sx=x+pad,shift=msb?7-sx%8:sx%8;
  unsigned bit=depth==1?((x/5+y/3)&1):(xy_colour(x,y,depth)>>(depth-1-p))&1;
  unsigned char *q=xy_bits+p*plane_stride+y*stride+sx/8;
  *q=(*q&~(1U<<shift))|(bit<<shift);
 }
 return (struct ps2_pipe_planar_source){xy_bits,bytes,stride,plane_stride,w+pad,h,depth,msb};
}
static void checked_xy(DrawablePtr d,int x,int y,unsigned w,unsigned h,unsigned pad,int format)
{
 unsigned depth=format==XYBitmap?1:d->depth,bits=d->bitsPerPixel==16?0x7fff:0xffffff;
 struct ps2_pipe_planar_source s=make_xy(w,h,pad,depth,BITMAP_BIT_ORDER==MSBFirst);
 struct pixels *p=model(destination_handle());size_t bytes=p->width*p->height*4;
 uint32_t *expected=malloc(bytes);unsigned char *copy=malloc(s.bytes?s.bytes:1);assert(expected && copy);
 memcpy(expected,p->data,bytes);memcpy(copy,s.bits,s.bytes);
 int ox=d->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=d->type==DRAWABLE_WINDOW?xp.screen_y:0;unsigned changed=0;
 for(unsigned j=0;j<p->height;++j)for(unsigned i=0;i<p->width;++i){
  int sx=(int)i+ox-x-d->x,sy=(int)j+oy-y-d->y;
  if(sx<0 || sy<0 || sx>=(int)w || sy>=(int)h || !pixman_region_contains_point(&clip_region,i+ox,j+oy,NULL))continue;
  uint32_t *q=expected+j*p->width+i,c=xy_colour(sx,sy,depth);
  *q=(*q&~(gc.planemask&bits))|(image_boolean(gc.alu,c,*q)&gc.planemask&bits);++changed;
 }
 unsigned sync=sync_calls,damage_before=damage_calls;
 gc.ops->PutImage(d,&gc,depth,x,y,w,h,pad,format,(char*)s.bits);
 check_pixels(expected,p,bits,gc.alu!=GXcopy || (gc.planemask&bits)!=bits);assert(!memcmp(copy,s.bits,s.bytes));
 unsigned emitted=changed && (gc.planemask&bits) && gc.alu!=GXnoop;
 assert(sync_calls==sync+emitted && damage_calls==damage_before+emitted);
 free(copy);free(expected);
}
static void xy_setup(int owned,unsigned bpp)
{image_setup(owned,bpp);gc.fgPixel=bpp==16?0x73e1:0xffcc12;gc.bgPixel=bpp==16?0x04bf:0x3120e4;}
static void xy_bitmap_case(void)
{xy_setup(0,32);checked_xy(&window.drawable,-7,-3,39,27,3,XYBitmap);gc.miTranslate=FALSE;
 checked_xy(&window.drawable,0,0,37,21,0,XYBitmap);assert(xy_fills && !image_uploads);image_finish();}
static void xy_planes_case(void)
{xy_setup(1,32);checked_xy(&window.drawable,-5,-2,41,29,9,XYPixmap);xp.drawable.x=7;xp.drawable.y=9;
 checked_xy(&xp.drawable,12,23,31,17,1,XYPixmap);image_finish();}
static void xy_padding_case(void)
{xy_setup(1,32);for(unsigned pad=0;pad<32;++pad){checked_xy(&window.drawable,1,2,33,11,pad,XYBitmap);
 checked_xy(&window.drawable,1,2,33,11,pad,XYPixmap);}image_finish();}
static void xy_clipping_case(void)
{xy_setup(1,32);for(unsigned translate=0;translate<2;++translate){gc.miTranslate=translate;
 checked_xy(&window.drawable,63,-4,65,37,31,XYBitmap);checked_xy(&window.drawable,-21,49,71,31,7,XYPixmap);}image_finish();}
static void xy_rgb555_case(void)
{xy_setup(1,16);for(unsigned alu=0;alu<16;++alu){gc.alu=alu;gc.planemask=0x513f;
 checked_xy(&window.drawable,2,1,37,19,17,XYBitmap);checked_xy(&window.drawable,-1,5,33,17,31,XYPixmap);}image_finish();}
static void xy_boolean_case(void)
{xy_setup(1,32);for(unsigned alu=0;alu<16;++alu){gc.alu=alu;gc.planemask=0xf13f77;
 checked_xy(&window.drawable,1,2,21,13,13,XYBitmap);checked_xy(&window.drawable,1,2,21,13,5,XYPixmap);}image_finish();}
static void xy_bitorder_case(void)
{
 xy_setup(1,32);struct pixels *p=model(destination_handle());size_t bytes=p->width*p->height*4;
 uint32_t *expected=malloc(bytes);assert(expected);
 for(unsigned msb=0;msb<2;++msb)for(unsigned depth=1;depth<=24;depth+=23){
  struct ps2_pipe_planar_source s=make_xy(29,17,7,depth,msb);
  struct ps2_pipe_planar_box boxes[]={{7,0,11,13,29,17},{12,3,17,15,17,9}};
  memcpy(expected,p->data,bytes);
  for(unsigned b=0;b<2;++b)for(unsigned y=0;y<boxes[b].height;++y)for(unsigned x=0;x<boxes[b].width;++x){
   const struct ps2_pipe_planar_box *q=&boxes[b];uint32_t *v=expected+(q->y+y)*p->width+q->x+x;
   *v=(*v&~0xf13f77)|(image_boolean(GXxor,xy_colour(q->source_x+x-7,q->source_y+y,depth),*v)&0xf13f77);
  }
  assert(!ps2_pipe_planar_boxes(ctx,destination_handle(),32,&s,boxes,2,gc.fgPixel,gc.bgPixel,0xf13f77,GXxor));
  check_pixels(expected,p,0xffffff,1);
 }
 free(expected);image_finish();
}
static void xy_snapshot_case(void)
{
 xy_setup(0,32);struct ps2_pipe_planar_source s=make_xy(19,7,5,24,0);
 struct ps2_pipe_planar_box box={5,0,70,60,19,7};
 struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));
 mutate_source=xy_bits;mutate_bytes=s.bytes;xy_mutate_box=&box;xy_mutate_header=&s;
 assert(!ps2_pipe_planar_boxes(ctx,0,32,&s,&box,1,0,0,0xffffff,GXcopy));
 for(unsigned y=0;y<7;++y)for(unsigned x=0;x<19;++x)
  assert((model(0)->data[(60+y)*720+70+x]&0xffffff)==xy_colour(x,y,24));
 assert(box.x==600 && s.depth==99 && !r->count);image_finish();
}
static void xy_noop_case(void)
{
 xy_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 gc.alu=GXnoop;checked_xy(&window.drawable,0,0,19,7,2,XYBitmap);gc.alu=GXcopy;gc.planemask=0;
 checked_xy(&window.drawable,0,0,19,7,11,XYPixmap);gc.planemask=0xffffff;
 checked_xy(&window.drawable,400,400,19,7,31,XYPixmap);checked_xy(&window.drawable,0,0,0,0,0,XYBitmap);
 struct ps2_pipe_planar_source s={.depth=1};assert(!ps2_pipe_planar_boxes(ctx,0,32,&s,NULL,0,0,0,0xffffff,GXcopy));
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);image_finish();
}
static void xy_preflight_case(void)
{
 xy_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 reject_put(1,19,7,-1,XYBitmap);reject_put(1,19,7,32,XYBitmap);reject_put(24,19,7,0,XYBitmap);
 reject_put(1,19,7,0,XYPixmap);reject_put(24,2048,2048,31,XYPixmap);reject_put(24,19,7,0,99);
 struct ps2_pipe_planar_source original=make_xy(19,7,3,24,0),s;
 struct ps2_pipe_planar_box box={3,0,40,40,19,7},bad;
 for(unsigned i=0;i<9;++i){s=original;bad=box;
  switch(i){case 0:s.stride=0;break;case 1:s.plane_stride=1;break;case 2:s.depth=23;break;
   case 3:s.bytes=PS2_PIPE_MAX_PLANAR_BYTES+1;break;case 4:s.width=2080;break;case 5:s.bits=(void*)(UINTPTR_MAX-2);break;
   case 6:bad.source_x=22;break;case 7:bad.x=720;break;default:s.msb_first=2;break;}
  assert(ps2_pipe_planar_boxes(ctx,0,32,&s,&bad,1,0,0,0xffffff,GXcopy)==-1);
 }
 assert(ps2_pipe_planar_boxes(ctx,0,32,&original,&box,4097,0,0,0xffffff,GXcopy)==-1);
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);image_finish();
}
static void xy_allocation_case(void)
{
 xy_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 struct ps2_pipe_planar_source s=make_xy(19,7,5,24,0);struct ps2_pipe_planar_box box={5,0,40,40,19,7};
 for(int i=1;i<=3;++i){malloc_fail=i;mal_calls=0;assert(ps2_pipe_planar_boxes(ctx,0,32,&s,&box,1,0,0,0xffffff,GXxor)==-1 && errno==ENOMEM);malloc_fail=0;}
 allocation_failure=1;reject_put(1,19,7,3,XYBitmap);allocation_failure=0;
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);image_finish();
}
static void xy_budget_case(void)
{
 xy_setup(0,32);struct ps2_pipe_planar_source s=make_xy(129,129,0,1,0);
 for(unsigned y=0;y<129;++y)for(unsigned x=0;x<129;++x){unsigned char *q=xy_bits+y*s.stride+x/8;
  *q=(*q&~(1U<<(x%8)))|(((x+y)&1)<<(x%8));}
 struct ps2_pipe_planar_box box={0,0,0,0,129,129};struct ps2_render_client *r=ps2_pipe_context_render(ctx);
 assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 assert(ps2_pipe_planar_boxes(ctx,0,32,&s,&box,1,0xffffff,0,0xffffff,GXcopy)==-1 && errno==E2BIG);
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);image_finish();
}
static void xy_failure_case(void)
{
 xy_setup(0,32);struct ps2_pipe_planar_source s=make_xy(39,27,3,1,0);xy_fail=2;
 expect_fatal=1;if(!setjmp(fatal)){gc.ops->PutImage(&window.drawable,&gc,1,0,0,39,27,3,XYBitmap,(char*)s.bits);assert(0);}
 assert(xy_requests==2 && xy_fills==2 && !damage_calls && ps2_pipe_context_error(ctx)==EIO);
 struct ps2_pipe_planar_box box={3,0,70,60,19,7};unsigned before=xy_requests;
 assert(ps2_pipe_planar_boxes(ctx,0,32,&s,&box,1,gc.fgPixel,gc.bgPixel,0xffffff,GXcopy)==-1 && xy_requests==before);
 image_finish();
}
static void xy_rewrap_case(void)
{
 xy_setup(1,32);const GCOps *ops=gc.ops;gc.funcs->ChangeGC(&gc,GCForeground);
 gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);assert(gc.ops==ops);
 checked_xy(&window.drawable,0,0,19,7,3,XYBitmap);ps2_image_screen_cleanup(&image_owner);
 assert(!gc_screen.image_ops);assert(ps2_image_screen_prepare(&xs,&image_owner,&gc_screen));
 checked_xy(&window.drawable,0,0,19,7,11,XYPixmap);image_finish();
}
static void xy_cursor_case(void)
{
 xy_setup(0,32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);
 assert(!ps2_pipe_cursor_set(cursor,&im,90,80));struct ps2_pipe_planar_source s=make_xy(48,48,7,24,0);
 struct ps2_pipe_planar_box box={7,0,64,64,48,48};assert(!ps2_pipe_planar_boxes(ctx,0,32,&s,&box,1,0,0,0xffffff,GXcopy));
 for(unsigned y=0;y<48;++y)for(unsigned x=0;x<48;++x)
  background_pixels[(64+y)*720+64+x]=(background_pixels[(64+y)*720+64+x]&0xff000000)|xy_colour(x,y,24);
 golden(&im,90,80,0);assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);
 assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));golden(&im,90,80,0);image_finish();
}
static void xy_random_case(void)
{
 xy_setup(1,32);uint32_t state=347;
 for(unsigned i=0;i<40;++i){state=state*1664525U+1013904223U;gc.alu=state%16;gc.planemask=state&0xffffff;
  checked_xy(&window.drawable,(int)((state>>8)%33)-12,(int)((state>>16)%21)-9,
   (state>>3)%45+1,(state>>24)%23+1,(state>>20)%32,(i&1)?XYBitmap:XYPixmap);}
 image_finish();
}
int main(int argc,char **argv)
{
 assert(argc==2);
 if(!strcmp(argv[1],"bitmap"))xy_bitmap_case();else if(!strcmp(argv[1],"planes"))xy_planes_case();
 else if(!strcmp(argv[1],"padding"))xy_padding_case();else if(!strcmp(argv[1],"clipping"))xy_clipping_case();
 else if(!strcmp(argv[1],"rgb555"))xy_rgb555_case();else if(!strcmp(argv[1],"boolean"))xy_boolean_case();
 else if(!strcmp(argv[1],"bitorder"))xy_bitorder_case();else if(!strcmp(argv[1],"snapshot"))xy_snapshot_case();
 else if(!strcmp(argv[1],"noop"))xy_noop_case();else if(!strcmp(argv[1],"preflight"))xy_preflight_case();
 else if(!strcmp(argv[1],"allocation"))xy_allocation_case();else if(!strcmp(argv[1],"budget"))xy_budget_case();
 else if(!strcmp(argv[1],"failure"))xy_failure_case();else if(!strcmp(argv[1],"rewrap"))xy_rewrap_case();
 else if(!strcmp(argv[1],"cursor"))xy_cursor_case();else if(!strcmp(argv[1],"random"))xy_random_case();else assert(0);
 puts("native planar image case PASS");return 0;
}
