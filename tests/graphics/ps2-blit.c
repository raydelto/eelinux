/* SPDX-License-Identifier: MIT */
#include "previous-copy.c"
#include "ps2_blit_exa.h"
static int fail_registration;
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{ return fail_registration ? FALSE : original_register_private(key,type,size); }
static struct pipe_resource *color(enum pipe_format format,unsigned w,unsigned h)
{
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=format,.width0=w,.height0=h,
        .depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW};
    struct pipe_resource *r=screen->resource_create(screen,&t);assert(r);seed(model(((struct ps2_resource*)r)->handle));return r;
}
static struct pipe_blit_info info(struct pipe_resource *s,struct pipe_resource *d)
{
    struct pipe_blit_info r={.mask=PIPE_MASK_RGBA,.filter=PIPE_TEX_FILTER_NEAREST};
    r.src.resource=s;r.src.format=s->format;r.src.box=(struct pipe_box){0,0,0,s->width0,s->height0,1};
    r.dst.resource=d;r.dst.format=d->format;r.dst.box=(struct pipe_box){0,0,0,d->width0,d->height0,1};return r;
}
static uint32_t *blit_reference(const struct pipe_blit_info *r)
{
    struct pixels *s=model(((struct ps2_resource*)r->src.resource)->handle),*d=model(((struct ps2_resource*)r->dst.resource)->handle);
    uint32_t *src=malloc((size_t)s->width*s->height*4),*expected=malloc((size_t)d->width*d->height*4);assert(src && expected);
    memcpy(src,s->data,(size_t)s->width*s->height*4);memcpy(expected,d->data,(size_t)d->width*d->height*4);
    for(int y=r->dst.box.y;y<r->dst.box.y+r->dst.box.height;++y)
        for(int x=r->dst.box.x;x<r->dst.box.x+r->dst.box.width;++x) {
            if(!(r->mask&(PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B)) || (r->scissor_enable &&
                (x<r->scissor.minx || x>=r->scissor.maxx || y<r->scissor.miny || y>=r->scissor.maxy))) continue;
            /* Rational source mapping at pixel centers is independent of the
             * normalized STQ floats emitted by the driver. */
            int u=((int64_t)r->src.box.x*2*r->dst.box.width+
                (2*(int64_t)(x-r->dst.box.x)+1)*r->src.box.width)/(2*r->dst.box.width);
            int v=((int64_t)r->src.box.y*2*r->dst.box.height+
                (2*(int64_t)(y-r->dst.box.y)+1)*r->src.box.height)/(2*r->dst.box.height);
            assert(u>=0 && v>=0 && u<(int)s->width && v<(int)s->height);
            expected[y*d->width+x]=src[v*s->width+u];
        }
    free(src);return expected;
}
static void checked_blit(struct pipe_blit_info *b)
{
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);uint32_t target=r->target,depth=r->depth;
    int sr=b->src.resource->reference.count,dr=b->dst.resource->reference.count;
    uint32_t *expected=blit_reference(b);ctx->blit(ctx,b);assert(!ps2_pipe_context_error(ctx));
    struct pixels *d=model(((struct ps2_resource*)b->dst.resource)->handle);
    assert(!memcmp(expected,d->data,(size_t)d->width*d->height*4));free(expected);
    assert(r->target==target && r->depth==depth && sr==b->src.resource->reference.count && dr==b->dst.resource->reference.count);
    assert(color_resource->reference.count==3 && depth_resource->reference.count==3);
}
static void release_blit(struct pipe_resource **s,struct pipe_resource **d)
{ pipe_resource_reference(s,NULL);pipe_resource_reference(d,NULL);ctx->destroy(ctx);release_external();screen->destroy(screen); }
static void scale(void)
{
    setup(1);
    for(unsigned f=0;f<2;++f) {
        enum pipe_format fmt=f ? PIPE_FORMAT_R5G5B5X1_UNORM : PIPE_FORMAT_R8G8B8X8_UNORM;
        struct pipe_resource *s=color(fmt,33,21),*d=color(fmt,99,63);struct pipe_blit_info b=info(s,d);
        checked_blit(&b);b.dst.box=(struct pipe_box){5,3,0,66,42,1};checked_blit(&b);
        b.dst.box=(struct pipe_box){7,9,0,11,7,1};checked_blit(&b);
        b.dst.box=(struct pipe_box){1,1,0,99,7,1};b.dst.box.width=97;checked_blit(&b);
        pipe_resource_reference(&s,NULL);pipe_resource_reference(&d,NULL);
    }
    ctx->destroy(ctx);release_external();screen->destroy(screen);
}
static void flips(void)
{
    setup(1);struct pipe_resource *s=color(PIPE_FORMAT_R8G8B8X8_UNORM,33,21),*d=color(s->format,99,63);
    for(int y=-1;y<=1;y+=2) for(int x=-1;x<=1;x+=2) {
        struct pipe_blit_info b=info(s,d);b.src.box=(struct pipe_box){x<0 ? 31 : 2,y<0 ? 20 : 1,0,x*29,y*19,1};checked_blit(&b);
    }
    release_blit(&s,&d);
}
static void scissor(void)
{
    setup(1);struct pipe_resource *s=color(PIPE_FORMAT_R8G8B8X8_UNORM,33,21),*d=color(s->format,99,63);
    struct pipe_blit_info b=info(s,d);b.src.box=(struct pipe_box){33,21,0,-33,-21,1};
    b.scissor_enable=TRUE;b.scissor=(struct pipe_scissor_state){7,9,86,58};checked_blit(&b);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    b.scissor=(struct pipe_scissor_state){100,70,101,71};checked_blit(&b);assert(calls==before && r->count==1);
    b.scissor=(struct pipe_scissor_state){0,0,0,0};checked_blit(&b);assert(calls==before && r->count==1);
    release_blit(&s,&d);
}
static void blit_copy(void)
{
    setup(1);struct pipe_blit_info b=info(color_resource,color_resource);
    b.src.box=(struct pipe_box){10,10,0,73,51,1};b.dst.box=(struct pipe_box){17,15,0,73,51,1};checked_blit(&b);
    assert(local_copies && textured_copies);ctx->destroy(ctx);release_external();screen->destroy(screen);
}
static void blit_preflight(void)
{
    for(unsigned mode=0;mode<17;++mode) {
        setup(1);struct pipe_resource *s=color(PIPE_FORMAT_R8G8B8X8_UNORM,33,21),*d=color(s->format,99,63);
        struct pipe_blit_info b=info(s,d);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
        assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;struct ps2_resource *native=(void*)s;uint32_t saved=native->handle;
        switch(mode) {
        case 0:b.src.box.x=-1;break;case 1:b.src.box.x=INT_MAX;break;case 2:b.src.box.width=INT_MIN;break;
        case 3:b.src.box.y=-1;break;case 4:b.src.box.height=INT16_MAX;break;case 5:b.src.box.width=0;break;
        case 6:b.dst.box.x=-1;break;case 7:b.dst.box.y=INT16_MAX;break;case 8:b.dst.box.width=-1;break;
        case 9:b.dst.box.height=INT16_MAX;break;case 10:b.dst.box.x=98;break;case 11:b.src.box.x=1;break;
        case 12:b.scissor_enable=TRUE;b.scissor=(struct pipe_scissor_state){20,10,19,30};break;
        case 13:native->handle=9999;break;case 14:s->width0=34;break;
        case 15:b.src.box.height=0;break;case 16:b.mask=0;b.src.box.width=INT_MIN;break;
        }
        ctx->blit(ctx,&b);assert(ps2_pipe_context_error(ctx)==EINVAL && calls==before && r->count==1);
        native->handle=saved;s->width0=33;release_blit(&s,&d);
    }
}
static void unsupported(void)
{
    for(unsigned mode=0;mode<17;++mode) {
        setup(1);struct pipe_resource *s=color(PIPE_FORMAT_R8G8B8X8_UNORM,33,21),*d=color(s->format,99,63);
        struct pipe_blit_info b=info(s,d);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
        struct ps2_resource *native=(void*)s;struct ps2_surface_client foreign={0};struct pipe_screen foreign_screen={0};
        struct ps2_surface_client *owner=native->owner;struct pipe_screen *saved_screen=s->screen;
        assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
        switch(mode) {
        case 0:b.filter=PIPE_TEX_FILTER_LINEAR;break;case 1:b.mask=PIPE_MASK_R;break;case 2:b.mask=PIPE_MASK_Z;break;
        case 3:b.alpha_blend=TRUE;break;case 4:b.src.level=1;break;case 5:b.dst.box.z=1;break;
        case 6:b.src.box.depth=2;break;case 7:b.src.format=PIPE_FORMAT_R8G8B8A8_UNORM;break;
        case 8:b.num_window_rectangles=1;break;case 9:b.dst_sample=1;break;case 10:s->nr_samples=4;break;
        case 11:s->target=PIPE_BUFFER;break;case 12:s->screen=&foreign_screen;break;case 13:native->owner=&foreign;break;
        case 14:b.dst.resource=s;b.dst.format=s->format;b.dst.box=(struct pipe_box){0,0,0,11,7,1};break;
        case 15:b.src.resource=NULL;break;case 16:b.dst.resource=NULL;break;
        }
        ctx->blit(ctx,&b);assert(ps2_pipe_context_error(ctx)==(mode==13 ? EXDEV : mode>=15 ? EINVAL : ENOTSUP));
        assert(calls==before && r->count==1);s->screen=saved_screen;native->owner=owner;s->nr_samples=0;s->target=PIPE_TEXTURE_2D;
        release_blit(&s,&d);
    }
}
static void shared(void)
{
    setup(1);texture_setup();struct pipe_resource *s=color(PIPE_FORMAT_R8G8B8X8_UNORM,33,21),*d=color(s->format,99,63);
    struct pipe_blit_info b=info(s,d);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    b.mask=PIPE_MASK_A;checked_blit(&b);b.mask=0;checked_blit(&b);assert(calls==before && r->count==1);
    b.mask=(PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B);checked_blit(&b);assert(!r->count && render_calls==1 && texture_view->reference.count==2);
    release_blit(&s,&d);
}
static struct ps2_exa_context_bridge ordering;
static struct ps2_texture_exa textures;
static struct ps2_blit_exa exa_blit;
static ExaDriverRec original_table,texture_table;
static void exa_setup(void)
{
    xorg_setup(1,0);ps2_glyph_gc_detach(&glyph_gc);original_table=exa.driver;
    assert(ps2_exa_context_bridge_prepare(&xs,&ordering,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    texture_table=exa.driver;assert(ps2_blit_exa_prepare(&xs,&exa_blit,&exa,ctx));seed(model(0));
}
static void exa_cleanup(void)
{
    ps2_blit_exa_cleanup(&xs,&exa_blit);ps2_blit_exa_cleanup(&xs,&exa_blit);assert(!memcmp(&exa.driver,&texture_table,sizeof(texture_table)));
    ps2_exa_context_bridge_cleanup(&ordering);assert(!memcmp(&exa.driver,&original_table,sizeof(original_table)));
    pixman_region_fini(&clip_region);ctx->destroy(ctx);release_external();screen->destroy(screen);
}
static void composite_test(void)
{
    exa_setup();int pitch;void *private=exa.driver.CreatePixmap2(&xs,129,65,24,0,32,&pitch);assert(private);
    PixmapRec owned={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=129,.height=65},.devPrivates=private};
    uint32_t handle=*(uint32_t*)private;seed(model(handle));
    for(unsigned mode=0;mode<3;++mode) {
        PixmapPtr src=mode==0 ? &owned : &xp,dst=mode==1 ? &owned : &xp;
        struct ps2_pipe_copy_box box={mode==0 ? 0 : 103,mode==0 ? 0 : 129,mode==1 ? 0 : 110,mode==1 ? 0 : 134,129,65};
        PictureRec a={.pDrawable=&src->drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest},b=a;b.pDrawable=&dst->drawable;
        uint32_t *expected=copy_reference(mode==0 ? handle : 0,mode==1 ? handle : 0,&box,1);
        assert(exa.driver.CheckComposite(PictOpSrc,&a,NULL,&b));
        assert(exa.driver.PrepareComposite(PictOpSrc,&a,NULL,&b,src,NULL,dst));
        exa.driver.Composite(dst,box.sx,box.sy,0,0,box.dx,box.dy,box.width,box.height);exa.driver.DoneComposite(dst);
        assert(!memcmp(expected,model(mode==1 ? handle : 0)->data,(size_t)dst->drawable.width*dst->drawable.height*4));free(expected);
    }
    exa.driver.DestroyPixmap(&xs,private);exa_cleanup();
}
static void blit_lifetime(void)
{
    exa_setup();ctx->destroy(ctx);release_external();screen->destroy(screen);unsigned before=calls;
    PictureRec a={.pDrawable=&xp.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};expect_fatal=1;
    if(!setjmp(fatal)) { exa.driver.PrepareComposite(PictOpSrc,&a,NULL,&a,&xp,NULL,&xp);assert(0); }
    assert(calls==before && !closes);ps2_blit_exa_cleanup(&xs,&exa_blit);assert(!closes);
    ps2_exa_context_bridge_cleanup(&ordering);assert(closes==1);pixman_region_fini(&clip_region);
}
static void blit_failure(void)
{
    setup(1);struct pipe_resource *s=color(PIPE_FORMAT_R8G8B8X8_UNORM,33,21),*d=color(s->format,99,63);
    struct pipe_blit_info b=info(s,d);fail_copy_at=1;ctx->blit(ctx,&b);assert(ps2_pipe_context_error(ctx)==EIO);
    unsigned before=calls;ctx->blit(ctx,&b);assert(calls==before);release_blit(&s,&d);assert(calls==before && closes==1);
}
static void installation(void)
{
    exa_setup();PictureRec a={.pDrawable=&xp.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};
    assert(exa.driver.CheckComposite(PictOpOver,&a,NULL,&a));assert(!exa.driver.CheckComposite(PictOpSrc,&a,&a,&a));
    a.repeat=1;assert(!exa.driver.CheckComposite(PictOpSrc,&a,NULL,&a));a.repeat=0;
    PictTransform matrix={0};a.transform=&matrix;assert(!exa.driver.CheckComposite(PictOpSrc,&a,NULL,&a));a.transform=NULL;
    a.filter=PictFilterBilinear;assert(!exa.driver.CheckComposite(PictOpSrc,&a,NULL,&a));a.filter=PictFilterNearest;
    assert(exa.driver.PrepareComposite(PictOpSrc,&a,NULL,&a,&xp,NULL,&xp));
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    expect_fatal=1;if(!setjmp(fatal)) { exa.driver.Composite(&xp,0,0,0,0,-1,0,13,9);assert(0); }
    assert(calls==before && r->count==1);exa.driver.DoneComposite(&xp);
    ps2_blit_exa_cleanup(&xs,&exa_blit);fail_registration=1;assert(!ps2_blit_exa_prepare(&xs,&exa_blit,&exa,ctx));fail_registration=0;
    assert(!memcmp(&exa.driver,&texture_table,sizeof(texture_table)));
    assert(ps2_blit_exa_prepare(&xs,&exa_blit,&exa,ctx));
    void (*saved)(PixmapPtr)=exa.driver.DoneComposite;exa.driver.DoneComposite=texture_table.DoneComposite;
    expect_fatal=1;if(!setjmp(fatal)) { ps2_blit_exa_cleanup(&xs,&exa_blit);assert(0); }
    assert(exa_blit.attached);exa.driver.DoneComposite=saved;exa_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"scale")) scale();else if(!strcmp(argv[1],"flips")) flips();
    else if(!strcmp(argv[1],"scissor")) scissor();else if(!strcmp(argv[1],"copy")) blit_copy();
    else if(!strcmp(argv[1],"preflight")) blit_preflight();else if(!strcmp(argv[1],"unsupported")) unsupported();
    else if(!strcmp(argv[1],"shared")) shared();else if(!strcmp(argv[1],"exa")) composite_test();
    else if(!strcmp(argv[1],"lifetime")) blit_lifetime();else if(!strcmp(argv[1],"failure")) blit_failure();
    else if(!strcmp(argv[1],"installation")) installation();else assert(0);
    puts("native Gallium blit and EXA composite test PASS");return 0;
}
