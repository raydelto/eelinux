/* SPDX-License-Identifier: MIT */
#include "previous-planar.c"
#include "ps2_polygon.h"
#include "ps2_polygon_gc.h"
static struct ps2_polygon_screen polygon_owner;
static struct ps2_pipe_polygon_point *change_points;
static struct ps2_pipe_solid_box *change_clips;
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
 if(op==PS2GS_RENDER_SUBMIT && change_points){change_points[0].x=600;change_points=NULL;change_clips[0].x1=600;change_clips=NULL;}
 return planar_ioctl(fd,op,arg);
}
void miReferenceFillPolygon(DrawablePtr,GCPtr,int,int,int,DDXPointPtr);
static uint32_t *polygon_expected;
static unsigned polygon_touched;
static void reference_spans(DrawablePtr d,GCPtr ref,int count,DDXPointPtr points,int *widths,int sorted)
{
 (void)sorted;struct pixels *p=model(destination_handle());
 int ox=d->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=d->type==DRAWABLE_WINDOW?xp.screen_y:0;
 unsigned bits=d->bitsPerPixel==16?0x7fff:0xffffff;
 for(int i=0;i<count;++i)for(int j=0;j<widths[i];++j){
  int x=points[i].x+j-ox,y=points[i].y-oy;
  if(x<0 || y<0 || x>=(int)p->width || y>=(int)p->height || !pixman_region_contains_point(&clip_region,x+ox,y+oy,NULL))continue;
  uint32_t *q=polygon_expected+y*p->width+x;
  *q=(*q&~(ref->planemask&bits))|(image_boolean(ref->alu,ref->fgPixel,*q)&ref->planemask&bits);++polygon_touched;
 }
}
static const GCOps reference_ops={.FillSpans=reference_spans};
static uint32_t *polygon_reference(DrawablePtr d,int shape,int mode,int n,const DDXPointRec *points)
{
 struct pixels *p=model(destination_handle());polygon_expected=malloc(p->width*p->height*4);assert(polygon_expected);
 memcpy(polygon_expected,p->data,p->width*p->height*4);polygon_touched=0;
 DDXPointRec *copy=malloc((n?n:1)*sizeof(*copy));assert(copy);if(n)memcpy(copy,points,n*sizeof(*copy));
 GC ref=gc;ref.ops=&reference_ops;miReferenceFillPolygon(d,&ref,shape,mode,n,copy);free(copy);return polygon_expected;
}
static void checked_polygon(DrawablePtr d,int shape,int mode,int n,DDXPointRec *points)
{
 uint32_t *expected=polygon_reference(d,shape,mode,n,points);DDXPointRec unchanged[4096];assert(n<=4096);
 if(n)memcpy(unchanged,points,n*sizeof(*points));
 unsigned sync=sync_calls,dam=damage_calls;
 gc.ops->FillPolygon(d,&gc,shape,mode,n,points);
 assert(!n || !memcmp(unchanged,points,n*sizeof(*points)));
 struct pixels *p=model(destination_handle());unsigned bits=d->bitsPerPixel==16?0x7fff:0xffffff;
 check_pixels(expected,p,bits,gc.alu!=GXcopy || (gc.planemask&bits)!=bits);
 unsigned emitted=polygon_touched && (gc.planemask&bits) && gc.alu!=GXnoop;
 assert(sync_calls==sync+emitted && damage_calls==dam+emitted);free(expected);
}
static void polygon_setup(int owned,unsigned bpp)
{xy_setup(owned,bpp);gc.fillStyle=FillSolid;gc.fillRule=EvenOddRule;assert(ps2_polygon_screen_prepare(&xs,&polygon_owner,&gc_screen));}
static void polygon_finish(void)
{ps2_polygon_screen_cleanup(&polygon_owner);ps2_polygon_screen_cleanup(&polygon_owner);image_finish();}
static DDXPointRec concave[]={{-7,4},{43,4},{43,-3},{72,23},{43,51},{43,38},{-7,38}};
static void polygon_convex(void)
{polygon_setup(0,32);DDXPointRec triangle[]={{-5,0},{59,17},{8,43}};checked_polygon(&window.drawable,Convex,CoordModeOrigin,3,triangle);
 DDXPointRec rectangle[]={{2,2},{29,2},{29,19},{2,19}};checked_polygon(&window.drawable,Convex,CoordModeOrigin,4,rectangle);assert(xy_fills);polygon_finish();}
static void polygon_concave(void)
{polygon_setup(1,32);checked_polygon(&window.drawable,Nonconvex,CoordModeOrigin,7,concave);polygon_finish();}
static void polygon_rules(void)
{
 polygon_setup(1,32);DDXPointRec rings[]={{0,0},{75,0},{75,57},{0,57},{0,0},{15,15},{60,15},{60,42},{15,42},{15,15},{0,0}};
 for(unsigned rule=0;rule<2;++rule){gc.fillRule=rule;checked_polygon(&window.drawable,Complex,CoordModeOrigin,11,rings);}
 DDXPointRec bow[]={{0,0},{64,53},{0,53},{64,0}};
 for(unsigned rule=0;rule<2;++rule){gc.fillRule=rule;checked_polygon(&window.drawable,Complex,CoordModeOrigin,4,bow);}
 polygon_finish();
}
static void polygon_relative(void)
{polygon_setup(1,32);DDXPointRec p[7];p[0]=concave[0];for(unsigned i=1;i<7;++i)p[i]=(DDXPointRec){concave[i].x-concave[i-1].x,concave[i].y-concave[i-1].y};
 for(unsigned translate=0;translate<2;++translate){gc.miTranslate=translate;checked_polygon(&window.drawable,Complex,CoordModePrevious,7,p);}polygon_finish();}
static void polygon_clipping(void)
{polygon_setup(0,32);DDXPointRec p[]={{-90,-70},{220,-20},{150,150},{-30,190}};
 for(unsigned rule=0;rule<2;++rule){gc.fillRule=rule;checked_polygon(&window.drawable,Convex,CoordModeOrigin,4,p);}polygon_finish();}
static void polygon_redirected(void)
{polygon_setup(1,32);checked_polygon(&window.drawable,Complex,CoordModeOrigin,7,concave);xp.drawable.x=7;xp.drawable.y=9;
 for(unsigned translate=0;translate<2;++translate){gc.miTranslate=translate;checked_polygon(&xp.drawable,Complex,CoordModeOrigin,7,concave);}polygon_finish();}
static void polygon_rgb555(void)
{polygon_setup(1,16);checked_polygon(&window.drawable,Complex,CoordModeOrigin,7,concave);
 for(unsigned alu=0;alu<16;++alu){gc.alu=alu;gc.planemask=0x513f;checked_polygon(&window.drawable,Complex,CoordModeOrigin,7,concave);}polygon_finish();}
static void polygon_boolean(void)
{polygon_setup(1,32);for(unsigned alu=0;alu<16;++alu)for(unsigned full=0;full<2;++full){gc.alu=alu;gc.planemask=full?0xffffff:0xf13f77;
 checked_polygon(&window.drawable,Complex,CoordModeOrigin,7,concave);}polygon_finish();}
static void polygon_snapshot(void)
{
 polygon_setup(0,32);struct ps2_pipe_polygon_point p[]={{64,64},{100,64},{100,91},{64,91}};
 struct ps2_pipe_solid_box clip={64,64,100,91};struct ps2_render_client *r=ps2_pipe_context_render(ctx);
 assert(!ps2_render_fill(r,0,0,2,2,0x80444444));change_points=p;change_clips=&clip;unsigned emitted=0;
 assert(!ps2_pipe_fill_polygon(ctx,0,32,p,4,&clip,1,0,0x765432,0xffffff,GXcopy,&emitted));
 assert(emitted==27 && p[0].x==600 && clip.x1==600 && !r->count);
 for(unsigned y=64;y<91;++y)for(unsigned x=64;x<100;++x)assert((model(0)->data[y*720+x]&0xffffff)==0x765432);
 polygon_finish();
}
static void polygon_noop(void)
{
 polygon_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 gc.alu=GXnoop;checked_polygon(&window.drawable,Complex,CoordModeOrigin,7,concave);gc.alu=GXcopy;gc.planemask=0;
 checked_polygon(&window.drawable,Complex,CoordModeOrigin,7,concave);gc.planemask=0xffffff;
 DDXPointRec far[]={{400,400},{420,400},{420,420},{400,420}},line[]={{0,0},{9,0},{19,0}};
 checked_polygon(&window.drawable,Complex,CoordModeOrigin,4,far);checked_polygon(&window.drawable,Complex,CoordModeOrigin,3,line);
 checked_polygon(&window.drawable,Complex,CoordModeOrigin,0,NULL);checked_polygon(&window.drawable,Complex,CoordModeOrigin,2,concave);
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);polygon_finish();
}
static void reject_polygon(int shape,int mode,int n,DDXPointPtr p)
{expect_fatal=1;if(!setjmp(fatal)){gc.ops->FillPolygon(&window.drawable,&gc,shape,mode,n,p);assert(0);}expect_fatal=0;}
static void polygon_preflight(void)
{
 polygon_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 reject_polygon(99,0,7,concave);reject_polygon(Complex,99,7,concave);reject_polygon(Complex,0,-1,concave);
 reject_polygon(Complex,0,4097,concave);reject_polygon(Complex,0,3,NULL);gc.fillStyle=FillTiled;reject_polygon(Complex,0,7,concave);gc.fillStyle=FillSolid;
 DDXPointRec overflow[]={{32760,0},{20,10},{-10,10}};reject_polygon(Complex,CoordModePrevious,3,overflow);
 struct ps2_pipe_polygon_point p[]={{0,0},{10,0},{10,10}};struct ps2_pipe_solid_box c[]={{0,0,20,20},{5,5,10,10}};
 assert(ps2_pipe_fill_polygon(ctx,0,32,p,3,c,1,2,1,0xffffff,GXcopy,NULL)==-1);
 assert(ps2_pipe_fill_polygon(ctx,0,32,p,3,c,2,0,1,0xffffff,GXcopy,NULL)==-1);
 assert(ps2_pipe_fill_polygon(ctx,0,32,(void*)(UINTPTR_MAX-2),3,c,1,0,1,0xffffff,GXcopy,NULL)==-1);
 p[0].x=65536;assert(ps2_pipe_fill_polygon(ctx,0,32,p,3,c,1,0,1,0xffffff,GXcopy,NULL)==-1);
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);polygon_finish();
}
static void polygon_allocation(void)
{
 polygon_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 struct ps2_pipe_polygon_point p[]={{40,40},{70,40},{70,70}};struct ps2_pipe_solid_box c={0,0,100,100};
 for(int i=1;i<=3;++i){malloc_fail=i;mal_calls=0;assert(ps2_pipe_fill_polygon(ctx,0,32,p,3,&c,1,0,1,0xffffff,GXcopy,NULL)==-1 && errno==ENOMEM);malloc_fail=0;}
 malloc_fail=1;mal_calls=0;reject_polygon(Complex,0,7,concave);malloc_fail=0;
 assert(attempts==before && r->count==1 && !xy_calls && !xy_fills);polygon_finish();
}
static void polygon_budget(void)
{
 polygon_setup(0,32);struct ps2_pipe_polygon_point p[4096];unsigned count=0;
 p[count++]=(struct ps2_pipe_polygon_point){0,128};p[count++]=(struct ps2_pipe_polygon_point){720,128};p[count++]=(struct ps2_pipe_polygon_point){720,0};
 for(int i=179;i>=0;--i){p[count++]=(struct ps2_pipe_polygon_point){i*4+2,0};p[count++]=(struct ps2_pipe_polygon_point){i*4+2,120};
  p[count++]=(struct ps2_pipe_polygon_point){i*4,120};p[count++]=(struct ps2_pipe_polygon_point){i*4,0};}
 struct ps2_pipe_solid_box c={0,0,720,576};struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));unsigned before=attempts;
 /* Exceed run count without exceeding edge-test budget. */
 assert(ps2_pipe_fill_polygon(ctx,0,32,p,count,&c,1,0,1,0xffffff,GXcopy,NULL)==-1 && errno==E2BIG);
 /* A wider 2048-high owned target exercises the edge-work bound. */
 uint32_t handle;assert(!ps2_surface_create(r->gs,64,2048,32,&handle));c=(struct ps2_pipe_solid_box){0,0,64,2048};
 for(unsigned i=0;i<4096;++i)p[i]=(struct ps2_pipe_polygon_point){i%2?60:0,i%2?2048:0};
 unsigned after_create=attempts;
 assert(ps2_pipe_fill_polygon(ctx,handle,32,p,4096,&c,1,1,1,0xffffff,GXcopy,NULL)==-1 && errno==E2BIG);
 assert(attempts==after_create && r->count==1 && !xy_calls && !xy_fills);assert(after_create>before);
 assert(!ps2_surface_destroy(r->gs,handle));polygon_finish();
}
static void polygon_failure(void)
{
 polygon_setup(0,32);xy_fail=2;expect_fatal=1;if(!setjmp(fatal)){gc.ops->FillPolygon(&window.drawable,&gc,Complex,0,7,concave);assert(0);}
 assert(xy_requests==2 && xy_fills==2 && !damage_calls && ps2_pipe_context_error(ctx)==EIO);
 unsigned before=xy_requests;struct ps2_pipe_polygon_point p[]={{40,40},{70,40},{70,70}};struct ps2_pipe_solid_box c={0,0,100,100};
 assert(ps2_pipe_fill_polygon(ctx,0,32,p,3,&c,1,0,1,0xffffff,GXcopy,NULL)==-1 && xy_requests==before);polygon_finish();
}
static void polygon_rewrap(void)
{polygon_setup(1,32);const GCOps *ops=gc.ops;gc.funcs->ChangeGC(&gc,GCForeground);gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);assert(gc.ops==ops);
 checked_polygon(&window.drawable,Complex,0,7,concave);ps2_polygon_screen_cleanup(&polygon_owner);assert(!gc_screen.polygon_ops);
 assert(ps2_polygon_screen_prepare(&xs,&polygon_owner,&gc_screen));checked_polygon(&window.drawable,Complex,0,7,concave);polygon_finish();}
static void polygon_cursor(void)
{
 polygon_setup(0,32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);struct ps2_cursor_image im=image(32,0);assert(!ps2_pipe_cursor_set(cursor,&im,90,80));
 struct ps2_pipe_polygon_point p[]={{64,64},{112,64},{112,112},{64,112}};struct ps2_pipe_solid_box c={0,0,720,576};
 assert(!ps2_pipe_fill_polygon(ctx,0,32,p,4,&c,1,0,0x765432,0xffffff,GXcopy,NULL));
 for(unsigned y=64;y<112;++y)for(unsigned x=64;x<112;++x)background_pixels[y*720+x]=(background_pixels[y*720+x]&0xff000000)|0x765432;
 golden(&im,90,80,0);assert(!ps2_pipe_cursor_flush(cursor));golden(&im,90,80,1);assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));golden(&im,90,80,0);polygon_finish();
}
static void polygon_random(void)
{polygon_setup(1,32);uint32_t state=823;
 for(unsigned k=0;k<80;++k){DDXPointRec p[13];for(unsigned i=0;i<13;++i){state=state*1664525U+1013904223U;p[i]=(DDXPointRec){(int)(state%130)-25,(int)((state>>12)%95)-17};}
 gc.alu=k%16;gc.planemask=k%3?0xf13f77:0xffffff;gc.fillRule=k%2;checked_polygon(&window.drawable,Complex,0,13,p);}polygon_finish();}
static void polygon_extreme(void)
{polygon_setup(0,32);gc.miTranslate=FALSE;DDXPointRec p[]={{-32768,-32768},{32767,-32768},{32767,32767},{-32768,32767}};
 checked_polygon(&window.drawable,Convex,0,4,p);DDXPointRec flat[]={{0,0},{0,0},{0,0}};checked_polygon(&window.drawable,Complex,0,3,flat);polygon_finish();}
int main(int argc,char **argv)
{
 assert(argc==2);
#define CASE(name) if(!strcmp(argv[1],#name))polygon_##name();else
 CASE(convex) CASE(concave) CASE(rules) CASE(relative) CASE(clipping) CASE(redirected) CASE(rgb555) CASE(boolean) CASE(snapshot)
 CASE(noop) CASE(preflight) CASE(allocation) CASE(budget) CASE(failure) CASE(rewrap) CASE(cursor) CASE(random) CASE(extreme) assert(0);
 puts("native polygon case PASS");return 0;
}
