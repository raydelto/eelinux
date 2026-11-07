/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_gc.h"
#include "dixfont.h"
#include "previous-glyph.c"

/* Mutable lower tables model EXA/damage unwrap/rewrap contracts, not an X
 * server or its rasterizer. Native glyph rendering and Mesa lifetimes are real. */
static struct ps2_gc_screen gc_screen;
static GCFuncs lower_funcs[2];
static GCOps lower_ops[2];
static unsigned creates_gc, destroys_gc, validates_gc, changes_gc, copies_gc;
static unsigned changes_clip, destroys_clip, copies_clip, draw_delegates, lookup_calls, close_calls;
static int create_failure, allocation_failure, font_excess, font_null;
static FontEncoding last_encoding;
static RegionRec returned_region;
static const GCFuncs *installed_funcs;
static const GCOps *installed_ops;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size)
{
    if (allocation_failure) { allocation_failure=0; errno=ENOMEM; return NULL; }
    return __real_calloc(count, size);
}
static void lower_state(GCPtr g)
{
    assert(g->funcs==&lower_funcs[0] || g->funcs==&lower_funcs[1]);
    assert(g->ops==&lower_ops[0] || g->ops==&lower_ops[1]);
}
static void replace_tables(GCPtr g)
{
    unsigned next=g->funcs==&lower_funcs[0];
    g->funcs=&lower_funcs[next]; g->ops=&lower_ops[next];
}
static void lower_validate(GCPtr g,unsigned long changes,DrawablePtr d)
{
    lower_state(g);
    assert(changes==(GCAllBits&~(GCTile|GCStipple)) && d==&window.drawable);
    /* Match EXA's actual trigger: FillTiled alone maps a colour tile. */
    assert(g->fillStyle==FillSolid && g->tileIsPixel && !g->stipple);
    ++validates_gc;replace_tables(g);
}
static void lower_change(GCPtr g,unsigned long mask)
{ lower_state(g); assert(mask==GCForeground); ++changes_gc; replace_tables(g); }
static void lower_copy(GCPtr source,unsigned long mask,GCPtr g)
{ lower_state(g); assert(source==&gc && mask==GCForeground); g->fgPixel=source->fgPixel; ++copies_gc; replace_tables(g); }
static void lower_destroy(GCPtr g)
{ lower_state(g); ++destroys_gc; }
static void lower_change_clip(GCPtr g,int type,void *value,int n)
{ lower_state(g); assert(type==CT_REGION && value==&clip_region && n==2); ++changes_clip; replace_tables(g); }
static void lower_destroy_clip(GCPtr g)
{ lower_state(g); ++destroys_clip; replace_tables(g); }
static void lower_copy_clip(GCPtr g,GCPtr source)
{ lower_state(g); assert(source==&gc); ++copies_clip; replace_tables(g); }
static void lower_rect(DrawablePtr d,GCPtr g,int n,xRectangle *r)
{ lower_state(g); assert(d==&window.drawable && n==1 && r->width==4); ++draw_delegates; replace_tables(g); }
static RegionPtr lower_area(DrawablePtr source,DrawablePtr d,GCPtr g,int x,int y,int w,int h,int dx,int dy)
{
    lower_state(g); assert(source==d && d==&window.drawable && x==1 && y==2 && w==3 && h==4 && dx==5 && dy==6);
    ++draw_delegates; replace_tables(g); return &returned_region;
}
static int forbidden_text(DrawablePtr d,GCPtr g,int x,int y,int n,char *chars)
{ (void)d;(void)g;(void)x;(void)y;(void)n;(void)chars; assert(0); return 0; }
static Bool lower_create(GCPtr g)
{
    ++creates_gc;
    if(create_failure) return FALSE;
    g->funcs=&lower_funcs[0]; g->ops=&lower_ops[0]; return TRUE;
}
static Bool lower_close(ScreenPtr s)
{
    assert(s==&xs && !gc_screen.installed && !gc_screen.count);
    assert(s->CreateGC==lower_create && s->CloseScreen==lower_close);
    ++close_calls; return FALSE;
}
static int font_lookup(FontPtr f,unsigned long n,unsigned char *chars,FontEncoding encoding,
                        unsigned long *count,CharInfoPtr *glyphs)
{
    assert(f==&font && chars); ++lookup_calls; last_encoding=encoding;
    *count=font_excess ? n+1 : n;
    for(unsigned long i=0;i<n;++i) glyphs[i]=font_null ? NULL : &character;
    return 0;
}
void GetGlyphs(FontPtr f,unsigned long n,unsigned char *chars,FontEncoding encoding,
                unsigned long *count,CharInfoPtr *glyphs)
{ f->get_glyphs(f,n,chars,encoding,count,glyphs); }
static void gc_setup(int owned)
{
    xorg_setup(owned,0); ps2_glyph_gc_detach(&glyph_gc);
    for(unsigned i=0;i<2;++i) {
        lower_funcs[i]=(GCFuncs){lower_validate,lower_change,lower_copy,lower_destroy,
                                lower_change_clip,lower_destroy_clip,lower_copy_clip};
        lower_ops[i]=(GCOps){.PolyText8=forbidden_text,.PolyFillRect=lower_rect,.CopyArea=lower_area};
    }
    xs.CreateGC=lower_create; xs.CloseScreen=lower_close;
    font.get_glyphs=font_lookup;
    assert(ps2_gc_screen_prepare(&xs,&gc_screen,&exa,ctx));
    assert(!ps2_gc_screen_prepare(&xs,&gc_screen,&exa,ctx));
    assert(xs.CreateGC(&gc)); assert(gc_screen.count==1);
    gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
    installed_funcs=gc.funcs; installed_ops=gc.ops;
}
static void gc_cleanup(void)
{
    ps2_gc_screen_cleanup(&gc_screen); ps2_gc_screen_cleanup(&gc_screen);
    assert(!gc_screen.count && !gc_screen.installed);
    assert(xs.CreateGC==lower_create && xs.CloseScreen==lower_close);
    if(xp.devPrivates) exa.driver.DestroyPixmap(&xs,xp.devPrivates);
    pixman_region_fini(&clip_region);
    ctx->destroy(ctx); release_external(); screen->destroy(screen);
    if(!owns_screen) ps2_surface_close(&gs);
}
static void text_check(void)
{
    memset(drawn,0,sizeof(drawn));
    char chars[]="A";
    assert(gc.ops->PolyText8(&window.drawable,&gc,3,7,1,chars)==13);
    struct ps2_bitmap_box clips[]={{20,30,26,42},{28,30,32,42}};
    struct ps2_bitmap_glyph g={.x=22,.y=34,.width=9,.height=3,.bits=bitmap,.stride=4};
    reference(&g,clips,2,0,0x8000ffff,NULL,0);
    assert(gc.funcs==installed_funcs && gc.ops==installed_ops);
}
static void gc_text_case(void)
{
    gc_setup(0);
    for(unsigned i=0;i<3;++i) { gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable); text_check(); }
    assert(last_encoding==Linear8Bit && lookup_calls==3 && validates_gc==4);
    unsigned short chars[]={0x4100};
    assert(gc.ops->PolyText16(&window.drawable,&gc,3,7,1,chars)==13);
    assert(last_encoding==Linear16Bit);
    font.info.lastRow=1;
    gc.ops->ImageText16(&window.drawable,&gc,3,7,1,chars); assert(last_encoding==TwoD16Bit);
    gc.ops->ImageText8(&window.drawable,&gc,3,7,1,(char*)chars); assert(last_encoding==Linear8Bit);
    assert(damage_calls==6 && processed==6);
    gc.funcs->DestroyGC(&gc); assert(destroys_gc==1 && !gc_screen.count);
    gc_cleanup();
}
static GC second,third;
static void *second_privates[16],*third_privates[16];
static void extra(GCPtr g,void **privates)
{
    *g=gc; g->devPrivates=(void*)privates;
    assert(xs.CreateGC(g)); g->funcs->ValidateGC(g,GCAllBits,&window.drawable);
}
static void gc_changes_case(void)
{
    gc_setup(0); extra(&second,second_privates); extra(&third,third_privates);
    gc.funcs->ChangeGC(&gc,GCForeground); assert(gc.ops==&lower_ops[0] || gc.ops==&lower_ops[1]);
    gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable); text_check();
    second.funcs->CopyGC(&gc,GCForeground,&second);
    second.funcs->ValidateGC(&second,GCAllBits,&window.drawable);
    second.funcs->ChangeClip(&second,CT_REGION,&clip_region,2);
    second.funcs->ValidateGC(&second,GCAllBits,&window.drawable);
    second.funcs->CopyClip(&second,&gc);
    second.funcs->DestroyClip(&second);
    assert(changes_gc==1 && copies_gc==1 && changes_clip==1 && copies_clip==1 && destroys_clip==1);
    second.funcs->DestroyGC(&second); third.funcs->DestroyGC(&third); gc.funcs->DestroyGC(&gc);
    assert(destroys_gc==3 && !gc_screen.count); gc_cleanup();
}
static void gc_rewrap_case(void)
{
    gc_setup(0); xRectangle r={.width=4};
    gc.ops->PolyFillRect(&window.drawable,&gc,1,&r); text_check();
    assert(gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,1,2,3,4,5,6)==&returned_region);
    text_check(); assert(draw_delegates==2 && damage_calls==2);
    gc_cleanup();
}
static void gc_lifetime_case(void)
{
    gc_setup(1); extra(&second,second_privates);
    third=gc; third.devPrivates=(void*)third_privates;
    ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(!closes);
    unsigned before=calls,validations=validates_gc,created=creates_gc;
    expect_fatal=1;
    if(!setjmp(fatal)) { gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable); assert(0); }
    assert(calls==before && validates_gc==validations && !xs.CreateGC(&third) && creates_gc==created);
    assert(!xs.CloseScreen(&xs)); assert(close_calls==1 && closes==1);
    lower_state(&gc); lower_state(&second);
    gc.funcs->DestroyGC(&gc); second.funcs->DestroyGC(&second);
    assert(destroys_gc==2); pixman_region_fini(&clip_region);
    ps2_gc_screen_cleanup(&gc_screen); assert(closes==1);
}
static void gc_creation_case(void)
{
    gc_setup(0); second=gc; second.devPrivates=(void*)second_privates;
    unsigned created=creates_gc;
    allocation_failure=1; assert(!xs.CreateGC(&second) && creates_gc==created && gc_screen.count==1);
    create_failure=1; assert(!xs.CreateGC(&second) && creates_gc==created+1 && gc_screen.count==1);
    create_failure=0; assert(xs.CreateGC(&second) && gc_screen.count==2);
    second.funcs->DestroyGC(&second); assert(gc_screen.count==1);
    text_check(); gc_cleanup();
}
static void gc_limit_case(void)
{
    gc_setup(0);
    GC *gcs=calloc(1024,sizeof(*gcs)); void **privates=calloc(1024*16,sizeof(void*)); assert(gcs && privates);
    for(unsigned i=0;i<1023;++i) { gcs[i]=gc; gcs[i].devPrivates=(void*)(privates+i*16); assert(xs.CreateGC(&gcs[i])); }
    gcs[1023]=gc; gcs[1023].devPrivates=(void*)(privates+1023*16);
    unsigned created=creates_gc,before=calls;
    assert(!xs.CreateGC(&gcs[1023]) && gc_screen.count==1024 && creates_gc==created && calls==before);
    ps2_gc_screen_cleanup(&gc_screen);
    for(unsigned i=0;i<1023;++i) { lower_state(&gcs[i]); gcs[i].funcs->DestroyGC(&gcs[i]); }
    free(gcs); free(privates); gc_cleanup();
}
static void gc_invalid_case(void)
{
    gc_setup(0); char chars[]="A"; unsigned before=calls;
    int counts[]={-1,256};
    for(unsigned i=0;i<2;++i) {
        expect_fatal=1;
        if(!setjmp(fatal)) { gc.ops->PolyText8(&window.drawable,&gc,0,4,counts[i],chars); assert(0); }
    }
    assert(lookup_calls==0 && calls==before);
    font_excess=1; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyText8(&window.drawable,&gc,0,4,1,chars); assert(0); }
    font_excess=0; font_null=1; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyText8(&window.drawable,&gc,0,4,1,chars); assert(0); }
    font_null=0; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyText8(&window.drawable,&gc,INT_MAX,4,1,chars); assert(0); }
    assert(gc.ops->PolyText8(&window.drawable,&gc,12,4,0,NULL)==12 && calls==before);
    gc_cleanup();
}
static void gc_failure_case(void)
{
    gc_setup(1); char chars[]="A"; fail_ioctl=EIO; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyText8(&window.drawable,&gc,3,7,1,chars); assert(0); }
    unsigned before=calls,validated=validates_gc; fail_ioctl=0; expect_fatal=1;
    if(!setjmp(fatal)) { gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable); assert(0); }
    assert(calls==before && validates_gc==validated && !damage_calls);
    gc_cleanup(); assert(closes==1 && calls==before);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"text")) gc_text_case(); else if(!strcmp(argv[1],"changes")) gc_changes_case();
    else if(!strcmp(argv[1],"rewrap")) gc_rewrap_case(); else if(!strcmp(argv[1],"lifetime")) gc_lifetime_case();
    else if(!strcmp(argv[1],"creation")) gc_creation_case(); else if(!strcmp(argv[1],"limit")) gc_limit_case();
    else if(!strcmp(argv[1],"invalid")) gc_invalid_case(); else if(!strcmp(argv[1],"failure")) gc_failure_case();
    else assert(0);
    puts("native Xorg GC integration test PASS"); return 0;
}
