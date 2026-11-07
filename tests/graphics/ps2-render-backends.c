/* SPDX-License-Identifier: MIT */
#include "ps2_render_target.h"
#include "pipe/p_screen.h"
#undef ARRAY_SIZE /* Xorg defines its equivalent macro unconditionally. */
#include "ps2_render_bridge.h"
#define main previous_backends_main
#define __wrap_ioctl previous_ioctl
#define dixRegisterPrivateKey previous_register_private
#include "ps2-surface-backends.c"
#undef main
#undef __wrap_ioctl
#undef dixRegisterPrivateKey

static unsigned private_slots, render_calls;
static struct ps2gs_render_submit last_submit;
static struct ps2gs_command last_commands[64];
static struct ps2_render_client render;
/* Mesa's debug reference descriptor is informational; resource references
 * and destruction still execute the real upstream inline implementation. */
void debug_describe_resource(char *buf,const struct pipe_resource *resource)
{ assert(resource); buf[0]=0; }
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    assert(type==PRIVATE_SCREEN && !size);
    if (!key->initialized) *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type,.offset=private_slots++*sizeof(void*)};
    return TRUE;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op); void *argument=va_arg(args,void*); va_end(args);
    if (op==PS2GS_DEPTH_CREATE || op==PS2GS_RENDER_SUBMIT) {
        ++calls;
        if (retry_ioctl) { --retry_ioctl; errno=EAGAIN; return -1; }
        if (fail_ioctl) { errno=fail_ioctl; return -1; }
        if (op==PS2GS_DEPTH_CREATE) {
            struct ps2gs_depth_create *d=argument;
            d->handle=next_handle++; d->buffer_width=(d->width+63)&~63U;
            d->allocated_bytes=d->buffer_width*((d->height+63)&~63U)*2;
            if (corrupt_reply) ++d->buffer_width;
        } else {
            last_submit=*(struct ps2gs_render_submit*)argument;
            assert(last_submit.count && last_submit.count<=64);
            memcpy(last_commands,(const void*)(uintptr_t)last_submit.commands,last_submit.count*sizeof(last_commands[0]));
            ++render_calls;
        }
        return 0;
    }
    int result=previous_ioctl(fd,op,argument);
    if (!result && op==PS2GS_GET_CAPS) ((struct ps2gs_caps*)argument)->features=127;
    return result;
}
static void init(void)
{ assert(!ps2_surface_open(&gs,"/dev/ps2-gs")); assert(!ps2_render_init(&render,&gs)); }
static void transport_case(void)
{
    init(); uint32_t color,depth;
    assert(!ps2_surface_create(&gs,65,33,32,&color));
    assert(!ps2_render_depth_create(&render,65,33,32,&depth));
    assert(!ps2_render_bind(&render,color,depth)); assert(!ps2_render_clear_depth(&render));
    struct ps2gs_vertex v[3]={{.x=16,.y=16,.z=4},{.x=64*16,.y=16,.z=4},{.x=32*16,.y=32*16,.z=4}};
    assert(!ps2_render_triangle(&render,v,1));
    retry_ioctl=1; assert(!ps2_render_bind(&render,0,0) && render_calls==1 && sleeps==1);
    assert(last_submit.target==color && last_submit.depth==depth && last_submit.count==2);
    assert(last_commands[0].op==PS2GS_CLEAR_DEPTH && last_commands[1].flags==PS2GS_DEPTH_TEST);
    for (unsigned i=0;i<65;++i) assert(!ps2_render_fill(&render,0,0,1,1,0));
    assert(render_calls==2 && render.count==1); assert(!ps2_render_flush(&render) && render_calls==3);
    assert(!ps2_render_depth_destroy(&render,depth)); assert(!ps2_surface_destroy(&gs,color));
    assert(!ps2_render_fill(&render,0,0,1,1,0)); fail_ioctl=EIO;
    assert(ps2_render_flush(&render)==-1 && errno==EIO && !render.count);
    unsigned before=calls; fail_ioctl=0; assert(ps2_render_flush(&render)==-1 && calls==before);
    ps2_surface_close(&gs);
    init(); corrupt_reply=1;
    assert(ps2_render_depth_create(&render,1,1,32,&depth)==-1 && errno==EPROTO && !depth);
    ps2_surface_close(&gs);
}
static void resource_destroy(struct pipe_screen *screen,struct pipe_resource *r)
{
    assert(r->screen==screen);
    if (r->format==PIPE_FORMAT_Z16_UNORM) ps2_depth_resource_destroy(r); else ps2_resource_destroy(r);
}
static void mesa_render_case(void)
{
    init(); struct pipe_screen screen={.resource_destroy=resource_destroy};
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=128,.height0=96,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    struct pipe_resource *c=ps2_resource_create(&screen,&gs,&t); assert(c);
    t.format=PIPE_FORMAT_Z16_UNORM; t.bind=PIPE_BIND_DEPTH_STENCIL;
    struct pipe_resource *d=ps2_depth_resource_create(&screen,&render,&t,32); assert(d);
    struct ps2_render_target target; assert(!ps2_render_target_init(&target,&screen,&render));
    assert(!ps2_render_target_bind(&target,c,d)); assert(c->reference.count==2 && d->reference.count==2);
    union pipe_color_union color={.f={0,0,1,1}};
    assert(!ps2_render_target_clear(&target,NULL,&color,1));
    struct draw_stage *stage=ps2_owned_draw_stage_create((struct draw_context*)1,&target); assert(stage);
    assert(!ps2_owned_draw_stage_outputs(stage,2,0,1,1)); /* flush clear before state change */
    assert(render_calls==1 && last_commands[0].op==PS2GS_FILL && last_commands[1].op==PS2GS_CLEAR_DEPTH);
    size_t size=sizeof(struct vertex_header)+2*sizeof(float[4]);
    struct vertex_header *v[3]; struct prim_header p={0};
    for (unsigned i=0;i<3;++i) {
        v[i]=calloc(1,size); assert(v[i]); p.v[i]=v[i];
        v[i]->data[0][0]=i==1?100:10; v[i]->data[0][1]=i==2?90:10;
        v[i]->data[0][2]=0.25f; v[i]->data[0][3]=1;
        v[i]->data[1][1]=1; v[i]->data[1][3]=1;
    }
    stage->tri(stage,&p); assert(render.count==1); stage->flush(stage,0);
    assert(render_calls==2 && last_commands[0].v[0].z==49151 && last_commands[0].v[0].rgba==0x8000ff00);
    for (unsigned i=0;i<3;++i) free(v[i]);
    stage->destroy(stage);
    ps2_render_target_cleanup(&target); assert(c->reference.count==1 && d->reference.count==1);
    ps2_resource_destroy(c); ps2_depth_resource_destroy(d); assert(destroys==2);
    ps2_surface_close(&gs);
}
static void xorg_order_case(void)
{
    init(); ScreenRec screen={.GetScreenPixmap=get_front}; void *privates[2]={0};
    screen.devPrivates=(PrivatePtr)privates;
    PixmapRec front={.drawable={.pScreen=&screen,.depth=24,.bitsPerPixel=32,.width=720,.height=576}};
    front_pixmap=&front;
    struct ps2_surface_exa exa; assert(ps2_surface_exa_prepare(&screen,&exa,&gs));
    struct ps2_exa_render_bridge b; assert(ps2_exa_render_bridge_prepare(&screen,&b,&exa,&render));
    assert(!ps2_exa_render_bridge_prepare(&screen,&b,&exa,&render));
    assert(!ps2_render_fill(&render,0,0,1,1,0x800000ff));
    assert(exa.driver.PrepareSolid(&front,GXcopy,0xffffff,0x00ff00));
    assert(render_calls==1 && !render.count);
    exa.driver.Solid(&front,0,0,1,1); assert(last_fill.rgba==0x8000ff00);
    int pitch; assert(!ps2_render_fill(&render,0,0,1,1,0));
    void *p=exa.driver.CreatePixmap2(&screen,64,32,24,0,32,&pitch); assert(p && render_calls==2);
    assert(!ps2_render_fill(&render,0,0,1,1,0)); exa.driver.DestroyPixmap(&screen,p);
    assert(render_calls==3 && destroys==1);
    assert(!ps2_render_fill(&render,0,0,1,1,0)); fail_ioctl=EIO;
    expect_fatal=1;
    if (!setjmp(fatal)) { exa.driver.PrepareSolid(&front,GXcopy,0xffffff,0); assert(0); }
    assert(gs.error==EIO && !render.count); unsigned before=calls;
    fail_ioctl=0; ps2_surface_close(&gs); assert(calls==before);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"transport")) transport_case(); else if (!strcmp(argv[1],"mesa-render")) mesa_render_case();
    else if (!strcmp(argv[1],"xorg-order")) xorg_order_case(); else assert(0);
    puts("owned render backend test PASS"); return 0;
}
