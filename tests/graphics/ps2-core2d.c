/* SPDX-License-Identifier: MIT */
#include "previous-polygon.c"
#include "ps2_polygon_tile.h"
#include "ps2_tile.h"
static PixmapRec colour_tile;
static uint32_t colour_handle;
static float real_float(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
    if(op!=PS2GS_TEXTURE_SUBMIT)return polygon_ioctl(fd,op,arg);
    const struct ps2gs_texture_submit *r=arg;assert(fd==999 && r->count && !(r->count%2));
    struct pixels *s=model(r->texture),*d=model(r->target);unsigned tw=1,th=1;
    while(tw<s->width)tw*=2;
    while(th<s->height)th*=2;
    const struct ps2gs_texture_triangle *t=(void*)(uintptr_t)r->triangles;
    unsigned mask=r->bits_per_pixel==16?0x7fff:0xffffff,spare=r->bits_per_pixel==16?0x8000:0x80000000;
    for(unsigned i=0;i<r->count;i+=2){
        int x1=t[i].v[0].position.x/16,y1=t[i].v[0].position.y/16;
        int x2=t[i].v[2].position.x/16,y2=t[i].v[2].position.y/16;
        float u1=real_float(t[i].v[0].s)*tw,v1=real_float(t[i].v[0].t)*th;
        float u2=real_float(t[i].v[2].s)*tw,v2=real_float(t[i].v[2].t)*th;
        assert(x2>x1 && y2>y1 && x1>=0 && y1>=0 && x2<=(int)d->width && y2<=(int)d->height);
        for(int y=y1;y<y2;++y)for(int x=x1;x<x2;++x){
            int u=u1+((float)x-x1+.5f)*(u2-u1)/(x2-x1),v=v1+((float)y-y1+.5f)*(v2-v1)/(y2-y1);
            assert(u>=0 && v>=0 && u<(int)s->width && v<(int)s->height);
            d->data[y*d->width+x]=(s->data[v*s->width+u]&mask)|spare;
        }
    }
    return 0;
}
static unsigned phase(int64_t x,unsigned n){int64_t r=x%n;return r<0?r+n:r;}
static void tile_setup(int owned,unsigned bpp)
{
    polygon_setup(owned,bpp);int pitch;
    colour_tile=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=bpp==16?15:24,.bitsPerPixel=bpp,.width=13,.height=7}};
    colour_tile.devPrivates=exa.driver.CreatePixmap2(&xs,13,7,colour_tile.drawable.depth,0,bpp,&pitch);assert(colour_tile.devPrivates);
    memcpy(&colour_handle,colour_tile.devPrivates,4);struct pixels *p=model(colour_handle);
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x)p->data[y*13+x]=((x+1)*713+(y+3)*9331)&(bpp==16?0x7fff:0xffffff);
    gc.fillStyle=FillTiled;gc.tileIsPixel=FALSE;gc.tile.pixmap=&colour_tile;gc.patOrg=(DDXPointRec){-9,11};
}
static void tile_finish(void){exa.driver.DestroyPixmap(&xs,colour_tile.devPrivates);colour_tile.devPrivates=NULL;polygon_finish();}
static void tile_reference_spans(DrawablePtr d,GCPtr g,int n,DDXPointPtr points,int *widths,int sorted)
{
    (void)sorted;struct pixels *p=model(destination_handle()),*s=model(colour_handle);
    int ox=d->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=d->type==DRAWABLE_WINDOW?xp.screen_y:0;
    for(int i=0;i<n;++i)for(int j=0;j<widths[i];++j){int px=points[i].x+j,py=points[i].y,x=px-ox,y=py-oy;
        if(x<0 || y<0 || x>=(int)p->width || y>=(int)p->height || !pixman_region_contains_point(&clip_region,px,py,NULL))continue;
        unsigned u=phase((int64_t)px-d->x-g->patOrg.x,13),v=phase((int64_t)py-d->y-g->patOrg.y,7);
        if(g->alu!=GXnoop && g->planemask)polygon_expected[y*p->width+x]=s->data[v*13+u];
    }
}
static const GCOps tile_reference_ops={.FillSpans=tile_reference_spans};
static void check_tile(DrawablePtr d,int mode,int n,DDXPointPtr points)
{
    struct pixels *p=model(destination_handle());size_t bytes=p->width*p->height*4;
    polygon_expected=malloc(bytes);assert(polygon_expected);memcpy(polygon_expected,p->data,bytes);
    DDXPointRec saved[16],copy[16];assert(n<=16);memcpy(saved,points,n*sizeof(*points));memcpy(copy,points,n*sizeof(*points));
    GC ref=gc;ref.ops=&tile_reference_ops;miReferenceFillPolygon(d,&ref,Complex,mode,n,copy);
    gc.ops->FillPolygon(d,&gc,Complex,mode,n,points);
    assert(!memcmp(saved,points,n*sizeof(*points)));check_pixels(polygon_expected,p,d->depth==15?0x7fff:0xffffff,0);free(polygon_expected);
}
static void tiled(unsigned bpp,int rules,int relative)
{
    tile_setup(1,bpp);DDXPointRec ring[]={{0,0},{75,0},{75,57},{0,57},{0,0},{15,15},{60,15},{60,42},{15,42},{15,15},{0,0}};
    if(rules){for(unsigned rule=0;rule<2;++rule){gc.fillRule=rule;check_tile(&window.drawable,0,11,ring);}}
    else{DDXPointRec p[7];memcpy(p,concave,sizeof(p));if(relative)for(int i=6;i>0;--i){p[i].x-=p[i-1].x;p[i].y-=p[i-1].y;}
        for(unsigned t=0;t<2;++t){gc.miTranslate=t;check_tile(&window.drawable,relative,7,p);gc.patOrg.x+=17;}}
    tile_finish();
}
static void tile_noop(void)
{
    tile_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0x80332211));
    unsigned queued=r->count,before=attempts;gc.alu=GXnoop;check_tile(&window.drawable,0,7,concave);
    gc.alu=GXcopy;gc.planemask=0;check_tile(&window.drawable,0,7,concave);assert(r->count==queued && attempts==before);tile_finish();
}
static void tile_invalid(void)
{
    tile_setup(0,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0x80332211));
    unsigned before=attempts;gc.alu=GXxor;reject_polygon(Complex,0,7,concave);gc.alu=GXcopy;gc.planemask=0xf3f3f3;reject_polygon(Complex,0,7,concave);
    assert(r->count==1 && attempts==before);tile_finish();
}
static void tile_validation(void)
{
    tile_setup(1,32);
    PixmapRec mono={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.width=13,.height=7}};
    gc.stipple=&mono;
    unsigned before=validates_gc;
    const GCOps *ops=gc.ops;
    gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
    assert(validates_gc==before+1 && gc.ops==ops);
    assert(gc.fillStyle==FillTiled && !gc.tileIsPixel && gc.tile.pixmap==&colour_tile && gc.stipple==&mono);
    check_tile(&window.drawable,0,7,concave);
    gc.stipple=NULL;tile_finish();
}
static void plane_case(unsigned bpp,int owned)
{
    xy_setup(1,bpp);window.realized=TRUE;xs.SourceValidate=source_validate;
    struct ps2_pipe_planar_source image=make_xy(43,32,0,1,BITMAP_BIT_ORDER==MSBFirst);
    PixmapRec bitmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.bitsPerPixel=1,.width=43,.height=32},.devKind=image.stride,.devPrivate={.ptr=xy_bits}};
    if(owned){int pitch;bitmap.devPrivate.ptr=NULL;bitmap.devPrivates=exa.driver.CreatePixmap2(&xs,43,32,1,0,1,&pitch);assert(bitmap.devPrivates);
        struct ps2_mono_source source;bitmap.devKind=pitch;assert(ps2_surface_exa_mono_source(&bitmap,&source)==1);memcpy((void*)source.bits,xy_bits,image.bytes);}
    struct pixels *p=model(destination_handle());unsigned bits=bpp==16?0x7fff:0xffffff;size_t bytes=p->width*p->height*4;uint32_t *expected=malloc(bytes);assert(expected);
    for(unsigned alu=0;alu<16;++alu){memcpy(expected,p->data,bytes);gc.alu=alu;gc.planemask=alu%2?bits:(bpp==16?0x513f:0xf13f77);
        for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x){
            int sx=(int)x+xp.screen_x-window.drawable.x+7+3,sy=(int)y+xp.screen_y-window.drawable.y+3+2;
            if(sx<3 || sy<2 || sx>=40 || sy>=31 || !pixman_region_contains_point(&clip_region,x+xp.screen_x,y+xp.screen_y,NULL))continue;
            uint32_t *q=expected+y*p->width+x;*q=(*q&~gc.planemask)|(image_boolean(alu,xy_colour(sx,sy,1),*q)&gc.planemask);
        }
        RegionPtr exposure=gc.ops->CopyPlane(&bitmap.drawable,&window.drawable,&gc,3,2,37,29,-7,-3,1);
        if(exposure)RegionDestroy(exposure);
        for(unsigned i=0;i<p->width*p->height;++i)if((expected[i]&bits)!=(p->data[i]&bits)){
            fprintf(stderr,"CopyPlane mismatch alu=%u x=%u y=%u expected=%x actual=%x\n",alu,i%p->width,i/p->width,expected[i],p->data[i]);break;}
        check_pixels(expected,p,bits,alu!=GXcopy || gc.planemask!=bits);
    }
    free(expected);if(owned)exa.driver.DestroyPixmap(&xs,bitmap.devPrivates);image_finish();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"tile"))tiled(32,0,0);
    else if(!strcmp(argv[1],"rules"))tiled(32,1,0);
    else if(!strcmp(argv[1],"relative"))tiled(32,0,1);
    else if(!strcmp(argv[1],"rgb555"))tiled(16,0,0);
    else if(!strcmp(argv[1],"noop"))tile_noop();
    else if(!strcmp(argv[1],"invalid"))tile_invalid();
    else if(!strcmp(argv[1],"validation"))tile_validation();
    else if(!strcmp(argv[1],"plane"))plane_case(32,0);
    else if(!strcmp(argv[1],"plane-owned"))plane_case(32,1);
    else if(!strcmp(argv[1],"plane-rgb555"))plane_case(16,1);
    else assert(0);
    puts("native core 2D additions PASS");return 0;
}
