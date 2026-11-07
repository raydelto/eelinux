/* SPDX-License-Identifier: MIT */
#include "previous-polygon.c"
#include "ps2_polygon_stipple.h"
static unsigned char pattern_bits[8192];
static PixmapRec pattern_pixmap;
static struct ps2_pipe_mono_pattern *change_pattern;
static unsigned char *change_pattern_bits;
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
 if(op==PS2GS_RENDER_SUBMIT && change_pattern_bits){memset(change_pattern_bits,0,8192);change_pattern_bits=NULL;
  change_pattern->bits=NULL;change_pattern->width=99;change_pattern->stride=0;change_pattern=NULL;}
 return polygon_ioctl(fd,op,arg);
}
static unsigned pattern_phase(int64_t position,unsigned size)
{int64_t n=position%size;return n<0?n+size:n;}
static unsigned pattern_value(unsigned x,unsigned y)
{return (x*5+y*3+x*y)%7<3;}
static void pattern_setup(int owned,unsigned bpp,unsigned opaque)
{
 polygon_setup(owned,bpp);memset(pattern_bits,0xa7,sizeof(pattern_bits));
 pattern_pixmap=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.bitsPerPixel=1,.width=13,.height=7},
  .devKind=12,.devPrivate={.ptr=pattern_bits}};
 for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned shift=BITMAP_BIT_ORDER==MSBFirst?7-x%8:x%8;
  unsigned char *p=pattern_bits+y*12+x/8;*p=(*p&~(1U<<shift))|(pattern_value(x,y)<<shift);}
 gc.stipple=&pattern_pixmap;gc.fillStyle=opaque?FillOpaqueStippled:FillStippled;
 gc.patOrg=(DDXPointRec){-9,-11};
}
static void patterned_reference_spans(DrawablePtr d,GCPtr ref,int count,DDXPointPtr points,int *widths,int sorted)
{
 (void)sorted;struct pixels *p=model(destination_handle());unsigned bits=d->depth==15?0x7fff:0xffffff;
 int ox=d->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=d->type==DRAWABLE_WINDOW?xp.screen_y:0;
 struct ps2_mono_source owned={0};int native=ps2_surface_exa_mono_source(ref->stipple,&owned);assert(native>=0);
 const unsigned char *source=native?owned.bits:ref->stipple->devPrivate.ptr;
 for(int i=0;i<count;++i)for(int j=0;j<widths[i];++j){
  int px=points[i].x+j,py=points[i].y,x=px-ox,y=py-oy;
  if(x<0 || y<0 || x>=(int)p->width || y>=(int)p->height || !pixman_region_contains_point(&clip_region,px,py,NULL))continue;
  unsigned u=pattern_phase((int64_t)px-d->x-ref->patOrg.x,ref->stipple->drawable.width);
  unsigned v=pattern_phase((int64_t)py-d->y-ref->patOrg.y,ref->stipple->drawable.height);
  unsigned shift=BITMAP_BIT_ORDER==MSBFirst?7-u%8:u%8;
  int value=(source[v*ref->stipple->devKind+u/8]>>shift)&1;
  if(!value && ref->fillStyle==FillStippled)continue;
  uint32_t *q=polygon_expected+y*p->width+x,colour=value?ref->fgPixel:ref->bgPixel;
  *q=(*q&~(ref->planemask&bits))|(image_boolean(ref->alu,colour,*q)&ref->planemask&bits);++polygon_touched;
 }
}
static const GCOps patterned_reference_ops={.FillSpans=patterned_reference_spans};
static void check_pattern(DrawablePtr d,int shape,int mode,int count,DDXPointPtr input)
{
 struct pixels *p=model(destination_handle());size_t bytes=p->width*p->height*4;
 polygon_expected=malloc(bytes);assert(polygon_expected);memcpy(polygon_expected,p->data,bytes);polygon_touched=0;
 DDXPointRec copy[4096],unchanged[4096];assert(count<=4096);
 if(count){memcpy(copy,input,count*sizeof(*copy));memcpy(unchanged,input,count*sizeof(*input));}
 unsigned char original[8192];memcpy(original,pattern_bits,sizeof(original));
 GC ref=gc;ref.ops=&patterned_reference_ops;miReferenceFillPolygon(d,&ref,shape,mode,count,copy);
 unsigned sync=sync_calls,dam=damage_calls;struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
 gc.ops->FillPolygon(d,&gc,shape,mode,count,input);
 assert(!count || !memcmp(input,unchanged,count*sizeof(*input)));assert(!memcmp(original,pattern_bits,sizeof(original)));
 unsigned bits=d->depth==15?0x7fff:0xffffff;check_pixels(polygon_expected,p,bits,gc.alu!=GXcopy || (gc.planemask&bits)!=bits);
 unsigned emitted=polygon_touched && (gc.planemask&bits) && gc.alu!=GXnoop;
 assert(sync_calls==sync+emitted && damage_calls==dam+emitted && r->target==target && r->depth==depth);
 free(polygon_expected);
}
static void pattern_transparent(void)
{pattern_setup(0,32,0);check_pattern(&window.drawable,Nonconvex,0,7,concave);assert(xy_fills);polygon_finish();}
static void pattern_opaque(void)
{pattern_setup(1,32,1);check_pattern(&window.drawable,Nonconvex,0,7,concave);gc.bgPixel=gc.fgPixel;
 check_pattern(&window.drawable,Complex,0,7,concave);polygon_finish();}
static void pattern_rules(void)
{pattern_setup(1,32,1);DDXPointRec p[]={{0,0},{75,0},{75,57},{0,57},{0,0},{15,15},{60,15},{60,42},{15,42},{15,15},{0,0}};
 for(unsigned rule=0;rule<2;++rule){gc.fillRule=rule;check_pattern(&window.drawable,Complex,0,11,p);}polygon_finish();}
static void pattern_origins(void)
{pattern_setup(1,32,0);for(unsigned t=0;t<2;++t)for(int k=-23;k<30;k+=13){gc.miTranslate=t;gc.patOrg=(DDXPointRec){k,k+7};
 check_pattern(&window.drawable,Complex,0,7,concave);}polygon_finish();}
static void pattern_relative(void)
{pattern_setup(1,32,1);DDXPointRec p[7];p[0]=concave[0];for(unsigned i=1;i<7;++i)p[i]=(DDXPointRec){concave[i].x-concave[i-1].x,concave[i].y-concave[i-1].y};
 for(unsigned t=0;t<2;++t){gc.miTranslate=t;check_pattern(&window.drawable,Complex,CoordModePrevious,7,p);}polygon_finish();}
static void pattern_redirected(void)
{pattern_setup(1,32,0);check_pattern(&window.drawable,Complex,0,7,concave);xp.drawable.x=7;xp.drawable.y=9;
 for(unsigned t=0;t<2;++t){gc.miTranslate=t;check_pattern(&xp.drawable,Complex,0,7,concave);}polygon_finish();}
static void pattern_rgb555(void)
{pattern_setup(1,16,0);for(unsigned opaque=0;opaque<2;++opaque)for(unsigned alu=0;alu<16;++alu){gc.fillStyle=opaque?FillOpaqueStippled:FillStippled;
 gc.alu=alu;gc.planemask=alu%2?0x513f:0x7fff;check_pattern(&window.drawable,Complex,0,7,concave);}polygon_finish();}
static void pattern_boolean(void)
{pattern_setup(1,32,0);for(unsigned opaque=0;opaque<2;++opaque)for(unsigned alu=0;alu<16;++alu)for(unsigned full=0;full<2;++full){
 gc.fillStyle=opaque?FillOpaqueStippled:FillStippled;gc.alu=alu;gc.planemask=full?0xffffff:0xf13f77;
 check_pattern(&window.drawable,Complex,0,7,concave);}polygon_finish();}
static struct ps2_pipe_mono_pattern backend_pattern(unsigned msb)
{return (struct ps2_pipe_mono_pattern){pattern_bits,sizeof(pattern_bits),12,13,7,msb};}
static void pattern_snapshot(void)
{
 pattern_setup(0,32,1);struct ps2_pipe_polygon_point p[]={{64,64},{100,64},{100,91},{64,91}};
 struct ps2_pipe_solid_box c={64,64,100,91};unsigned char saved[8192];memcpy(saved,pattern_bits,sizeof(saved));
 struct ps2_pipe_mono_pattern mono=backend_pattern(1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
 assert(!ps2_render_fill(r,0,0,2,2,0x80444444));change_points=p;change_clips=&c;change_pattern=&mono;change_pattern_bits=pattern_bits;
 unsigned emitted=0;assert(!ps2_pipe_stipple_polygon(ctx,0,32,p,4,&c,1,0,&mono,1,-9,INT32_MIN,0x765432,0xabcdef,0xffffff,GXcopy,&emitted));
 assert(emitted && !mono.bits && p[0].x==600 && c.x1==600 && !r->count);
 for(unsigned y=64;y<91;++y)for(unsigned x=64;x<100;++x){unsigned u=pattern_phase((int64_t)x+9,13),v=pattern_phase((int64_t)y-INT32_MIN,7);
  uint32_t expected=(saved[v*12+u/8]&(0x80U>>(u%8)))?0x765432:0xabcdef;assert((model(0)->data[y*720+x]&0xffffff)==expected);}
 polygon_finish();
}
static void pattern_owned(void)
{
 pattern_setup(1,32,1);PixmapRec owned={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.bitsPerPixel=1}};int pitch;
 owned.devPrivates=exa.driver.CreatePixmap2(&xs,13,7,1,0,1,&pitch);assert(owned.devPrivates);
 assert(exa.driver.ModifyPixmapHeader(&owned,13,7,1,1,pitch,NULL));struct ps2_mono_source source;
 assert(ps2_surface_exa_mono_source(&owned,&source)==1);
 for(unsigned y=0;y<7;++y)memcpy((unsigned char*)source.bits+y*pitch,pattern_bits+y*12,2);
 owned.devPrivate.ptr=NULL;gc.stipple=&owned;check_pattern(&window.drawable,Complex,0,7,concave);
 exa.driver.DestroyPixmap(&xs,owned.devPrivates);polygon_finish();
}
static void pattern_noop(void)
{
 pattern_setup(0,32,0);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 gc.alu=GXnoop;check_pattern(&window.drawable,Complex,0,7,concave);gc.alu=GXcopy;gc.planemask=0;
 check_pattern(&window.drawable,Complex,0,7,concave);gc.planemask=0xffffff;memset(pattern_bits,0,sizeof(pattern_bits));
 check_pattern(&window.drawable,Complex,0,7,concave);check_pattern(&window.drawable,Complex,0,0,NULL);
 assert(attempts==before && r->count==1 && !xy_fills && !xy_calls);polygon_finish();
}
static void pattern_preflight(void)
{
 pattern_setup(0,32,1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 gc.stipple=NULL;reject_polygon(Complex,0,7,concave);gc.stipple=&pattern_pixmap;
 pattern_pixmap.drawable.width=257;reject_polygon(Complex,0,7,concave);pattern_pixmap.drawable.width=13;
 pattern_pixmap.devKind=1;reject_polygon(Complex,0,7,concave);pattern_pixmap.devKind=12;
 pattern_pixmap.devPrivate.ptr=NULL;reject_polygon(Complex,0,7,concave);pattern_pixmap.devPrivate.ptr=pattern_bits;
 struct ps2_pipe_polygon_point p[]={{40,40},{70,40},{70,70}};struct ps2_pipe_solid_box c={0,0,100,100};
 struct ps2_pipe_mono_pattern mono=backend_pattern(0);mono.bytes=1;
 assert(ps2_pipe_stipple_polygon(ctx,0,32,p,3,&c,1,0,&mono,1,0,0,1,2,0xffffff,GXcopy,NULL)==-1);mono=backend_pattern(0);mono.msb_first=2;
 assert(ps2_pipe_stipple_polygon(ctx,0,32,p,3,&c,1,0,&mono,1,0,0,1,2,0xffffff,GXcopy,NULL)==-1);mono=backend_pattern(0);mono.bits=(void*)(UINTPTR_MAX-1);
 assert(ps2_pipe_stipple_polygon(ctx,0,32,p,3,&c,1,0,&mono,1,0,0,1,2,0xffffff,GXcopy,NULL)==-1);
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);polygon_finish();
}
static void pattern_allocation(void)
{
 pattern_setup(0,32,1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 struct ps2_pipe_polygon_point p[]={{40,40},{70,40},{70,70}};struct ps2_pipe_solid_box c={0,0,100,100};struct ps2_pipe_mono_pattern mono=backend_pattern(0);
 for(int i=1;i<=5;++i){malloc_fail=i;mal_calls=0;unsigned emitted=99;
  assert(ps2_pipe_stipple_polygon(ctx,0,32,p,3,&c,1,0,&mono,1,0,0,1,2,0xffffff,GXcopy,&emitted)==-1 && errno==ENOMEM && !emitted);malloc_fail=0;}
 malloc_fail=1;mal_calls=0;reject_polygon(Complex,0,7,concave);malloc_fail=0;
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);polygon_finish();
}
static void pattern_budget(void)
{
 pattern_setup(0,16,1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
 memset(pattern_bits,0xaa,sizeof(pattern_bits));struct ps2_pipe_mono_pattern mono={pattern_bits,sizeof(pattern_bits),4,8,1,0};
 struct ps2_pipe_polygon_point p[]={{0,0},{720,0},{720,100},{0,100}};struct ps2_pipe_solid_box c={0,0,720,576};
 assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 assert(ps2_pipe_stipple_polygon(ctx,0,16,p,4,&c,1,0,&mono,1,0,0,1,2,0x7fff,GXcopy,NULL)==-1 && errno==E2BIG);
 assert(attempts==before && r->count==1);
 uint32_t handle;assert(!ps2_surface_create(r->gs,1024,1025,16,&handle));unsigned after=attempts;
 p[1].x=p[2].x=1024;p[2].y=p[3].y=1025;c=(struct ps2_pipe_solid_box){0,0,1024,1025};
 assert(ps2_pipe_stipple_polygon(ctx,handle,16,p,4,&c,1,0,&mono,1,0,0,1,1,0x7fff,GXcopy,NULL)==-1 && errno==E2BIG);
 assert(attempts==after && r->count==1);assert(!ps2_surface_destroy(r->gs,handle));polygon_finish();
}
static void pattern_failure(void)
{pattern_setup(0,32,1);xy_fail=2;expect_fatal=1;if(!setjmp(fatal)){gc.ops->FillPolygon(&window.drawable,&gc,Complex,0,7,concave);assert(0);}
 assert(xy_requests==2 && !damage_calls && ps2_pipe_context_error(ctx)==EIO);unsigned before=xy_requests;
 reject_polygon(Complex,0,7,concave);assert(xy_requests==before);polygon_finish();}
static void pattern_cursor(void)
{
 pattern_setup(0,32,1);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 struct ps2_pipe_polygon_point p[]={{64,64},{112,64},{112,112},{64,112}};struct ps2_pipe_solid_box c={0,0,720,576};struct ps2_pipe_mono_pattern mono=backend_pattern(0);
 assert(!ps2_pipe_stipple_polygon(ctx,0,32,p,4,&c,1,0,&mono,1,-9,-11,0x765432,0xabcdef,0xffffff,GXcopy,NULL));
 for(unsigned y=64;y<112;++y)for(unsigned x=64;x<112;++x){unsigned u=pattern_phase(x+9,13),v=pattern_phase(y+11,7);
  uint32_t colour=(pattern_bits[v*12+u/8]&(1U<<(u%8)))?0x765432:0xabcdef;background_pixels[y*720+x]=(background_pixels[y*720+x]&0xff000000)|colour;}
 golden(&im,90,80,0);assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));golden(&im,90,80,0);polygon_finish();
}
static void pattern_random(void)
{pattern_setup(1,32,1);uint32_t state=912;
 for(unsigned k=0;k<60;++k){DDXPointRec p[13];for(unsigned i=0;i<13;++i){state=state*1664525U+1013904223U;p[i]=(DDXPointRec){(int)(state%130)-25,(int)((state>>12)%95)-17};}
  gc.fillStyle=k%2?FillStippled:FillOpaqueStippled;gc.fillRule=(k/2)%2;gc.alu=k%16;gc.planemask=k%3?0xf13f77:0xffffff;
  check_pattern(&window.drawable,Complex,0,13,p);}polygon_finish();}
static void pattern_rewrap(void)
{pattern_setup(1,32,1);const GCOps *ops=gc.ops;gc.funcs->ChangeGC(&gc,GCForeground);
 gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);assert(gc.ops==ops);check_pattern(&window.drawable,Complex,0,7,concave);
 ps2_polygon_screen_cleanup(&polygon_owner);assert(!gc_screen.polygon_ops);assert(ps2_polygon_screen_prepare(&xs,&polygon_owner,&gc_screen));
 check_pattern(&window.drawable,Complex,0,7,concave);polygon_finish();}
int main(int argc,char **argv)
{
 assert(argc==2);
#undef CASE
#define CASE(name) if(!strcmp(argv[1],#name))pattern_##name();else
 CASE(transparent) CASE(opaque) CASE(rules) CASE(origins) CASE(relative) CASE(redirected) CASE(rgb555) CASE(boolean) CASE(snapshot)
 CASE(owned) CASE(noop) CASE(preflight) CASE(allocation) CASE(budget) CASE(failure) CASE(cursor) CASE(random) CASE(rewrap) assert(0);
 puts("native stippled polygon case PASS");return 0;
}
