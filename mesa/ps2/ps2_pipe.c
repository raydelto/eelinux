/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#include "ps2_copy_batch.h"
#include "ps2_blit.h"
#include "util/u_inlines.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct ps2_pipe_context;
struct ps2_pipe_screen {
    struct pipe_screen base;
    struct ps2_render_client *render;
    struct ps2_surface_client gs;
    struct ps2_render_client own_render;
    struct ps2_pipe_context *context;
    unsigned refs;
    int closing, owns_file;
};
struct ps2_pipe_context {
    struct pipe_context base;
    struct ps2_pipe_screen *screen;
    struct ps2_render_target target;
    struct pipe_surface *color, *depth;
    struct pipe_sampler_view *view;
    struct pipe_sampler_state sampler;
    uint32_t bound_color,bound_depth;
    unsigned refs;
    int closing, error, has_sampler;
};
static void context_destroy(struct pipe_context *);
static int valid_context(struct pipe_context *base)
{ return base && base->destroy==context_destroy; }
static int reject(struct ps2_pipe_context *c,int error)
{
    if (!c->error) c->error=error;
    errno=c->error;
    return -1;
}
static int ready(struct ps2_pipe_context *c)
{
    if (c->closing) return reject(c,ESHUTDOWN);
    if (c->screen->render->gs->error) return reject(c,c->screen->render->gs->error);
    return c->error ? reject(c,c->error) : 0;
}
static void screen_unref(struct ps2_pipe_screen *s)
{
    if (--s->refs) return;
    if (s->owns_file) ps2_surface_close(&s->gs);
    free(s);
}
static void context_unref(struct ps2_pipe_context *c)
{
    if (--c->refs) return;
    struct ps2_pipe_screen *s=c->screen;
    s->context=NULL;
    free(c);
    screen_unref(s);
}
int ps2_pipe_context_acquire(struct pipe_context *base)
{
    if (!valid_context(base)) { errno=EINVAL; return -1; }
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return -1;
    ++c->refs; return 0;
}
void ps2_pipe_context_release(struct pipe_context *base)
{ if (valid_context(base)) context_unref((void*)base); }
struct ps2_render_client *ps2_pipe_context_render(struct pipe_context *base)
{ return valid_context(base) ? ((struct ps2_pipe_context*)base)->screen->render : NULL; }
struct ps2_render_target *ps2_pipe_context_target(struct pipe_context *base)
{ return valid_context(base) ? &((struct ps2_pipe_context*)base)->target : NULL; }
int ps2_pipe_context_error(struct pipe_context *base)
{
    if (!valid_context(base)) return EINVAL;
    struct ps2_pipe_context *c=(void*)base;
    return c->error ? c->error : c->screen->render->gs->error;
}
static bool format_supported(struct pipe_screen *base,enum pipe_format format,
    enum pipe_texture_target target,unsigned samples,unsigned storage_samples,unsigned bind)
{
    struct ps2_pipe_screen *s=(void*)base;
    if (s->closing || target!=PIPE_TEXTURE_2D || samples>1 || storage_samples>1) return false;
    if (format==PIPE_FORMAT_Z16_UNORM) return bind==PIPE_BIND_DEPTH_STENCIL;
    return (format==PIPE_FORMAT_R8G8B8X8_UNORM || format==PIPE_FORMAT_R5G5B5X1_UNORM) &&
        !(bind & ~(PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW));
}
static struct pipe_resource *resource_create(struct pipe_screen *base,const struct pipe_resource *t)
{
    struct ps2_pipe_screen *s=(void*)base;
    if (!t || !format_supported(base,t->format,t->target,t->nr_samples,t->nr_storage_samples,t->bind)) {
        errno=ENOTSUP; return NULL;
    }
    struct pipe_resource *r=t->format==PIPE_FORMAT_Z16_UNORM ?
        ps2_depth_resource_create(base,s->render,t,s->render->gs->caps.bits_per_pixel) :
        ps2_resource_create(base,s->render->gs,t);
    if (r) ++s->refs;
    return r;
}
static void resource_destroy(struct pipe_screen *base,struct pipe_resource *r)
{
    struct ps2_pipe_screen *s=(void*)base;
    /* Fence even an unbound resource before releasing its kernel handle. */
    if (!s->render->gs->error) ps2_render_flush(s->render);
    if (r->format==PIPE_FORMAT_Z16_UNORM) ps2_depth_resource_destroy(r);
    else ps2_resource_destroy(r);
    screen_unref(s);
}
static void screen_destroy(struct pipe_screen *base)
{
    struct ps2_pipe_screen *s=(void*)base;
    if (s->closing) return;
    s->closing=1; screen_unref(s);
}
static const char *name(struct pipe_screen *base) { (void)base; return "PS2 Graphics Synthesizer"; }
static const char *vendor(struct pipe_screen *base) { (void)base; return "Sony"; }
static int parameter(struct pipe_screen *base,enum pipe_cap cap)
{ (void)base; (void)cap; return 0; }
static float parameterf(struct pipe_screen *base,enum pipe_capf cap)
{ (void)base; (void)cap; return 0; }
static int shader_parameter(struct pipe_screen *base,enum pipe_shader_type shader,enum pipe_shader_cap cap)
{ (void)base; (void)shader; (void)cap; return 0; }

static struct pipe_surface *surface_create(struct pipe_context *base,struct pipe_resource *r,
    const struct pipe_surface *t)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return NULL;
    if (!r || !t || r->screen!=base->screen || t->format!=r->format ||
        t->u.tex.level || t->u.tex.first_layer || t->u.tex.last_layer ||
        !(r->bind&(PIPE_BIND_RENDER_TARGET|PIPE_BIND_DEPTH_STENCIL))) { errno=ENOTSUP; return NULL; }
    struct pipe_surface *surface=calloc(1,sizeof(*surface));
    if (!surface) return NULL;
    *surface=*t; surface->texture=NULL;
    surface->context=base; surface->width=r->width0; surface->height=r->height0;
    pipe_reference_init(&surface->reference,1);
    pipe_resource_reference(&surface->texture,r);
    ++c->refs;
    return surface;
}
static void surface_destroy(struct pipe_context *base,struct pipe_surface *surface)
{
    struct ps2_pipe_context *c=(void*)base;
    pipe_resource_reference(&surface->texture,NULL); free(surface); context_unref(c);
}
static struct pipe_sampler_view *view_create(struct pipe_context *base,struct pipe_resource *r,
    const struct pipe_sampler_view *t)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return NULL;
    struct pipe_sampler_view *view=ps2_sampler_view_create(base,c->screen->render,r,t);
    if (view) ++c->refs;
    return view;
}
static void view_destroy(struct pipe_context *base,struct pipe_sampler_view *view)
{ ps2_sampler_view_destroy(base,view); context_unref((void*)base); }
static int bind_target(struct ps2_pipe_context *c)
{
    if (ready(c)) return -1;
    if (!c->color) return reject(c,EINVAL);
    struct pipe_resource *color=c->color->texture,*depth=c->depth ? c->depth->texture : NULL;
    struct ps2_render_client *render=c->screen->render;
    if (c->target.color==color && c->target.depth==depth &&
        render->target==c->bound_color && render->depth==c->bound_depth) return 0;
    if (ps2_render_target_bind(&c->target,color,depth))
        return reject(c,errno);
    c->bound_color=render->target; c->bound_depth=render->depth;
    return 0;
}
static void framebuffer(struct pipe_context *base,const struct pipe_framebuffer_state *fb)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if (!fb || fb->nr_cbufs>1 || fb->samples>1 || fb->layers>1 ||
        (!fb->nr_cbufs && fb->zsbuf)) { reject(c,ENOTSUP); return; }
    struct pipe_surface *color=fb->nr_cbufs ? fb->cbufs[0] : NULL,*depth=fb->zsbuf;
    if (color && (color->context!=base || color->texture->screen!=base->screen ||
        !(color->texture->bind&PIPE_BIND_RENDER_TARGET) || color->width!=fb->width || color->height!=fb->height)) {
        reject(c,EINVAL); return;
    }
    if (depth && (depth->context!=base || depth->format!=PIPE_FORMAT_Z16_UNORM ||
        depth->width!=fb->width || depth->height!=fb->height || !color ||
        (color->format==PIPE_FORMAT_R8G8B8X8_UNORM ? 32U : 16U)!=c->screen->render->gs->caps.bits_per_pixel)) {
        reject(c,EINVAL); return;
    }
    if (ps2_render_flush(c->screen->render)) { reject(c,errno); return; }
    pipe_surface_reference(&c->color,color); pipe_surface_reference(&c->depth,depth);
    if (!color) ps2_render_target_cleanup(&c->target);
    else bind_target(c);
}
static void *sampler_create(struct pipe_context *base,const struct pipe_sampler_state *state)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return NULL;
    if (!ps2_sampler_state_supported(state)) { errno=ENOTSUP; return NULL; }
    struct pipe_sampler_state *copy=malloc(sizeof(*copy));
    if (copy) *copy=*state;
    return copy;
}
static void sampler_delete(struct pipe_context *base,void *state) { (void)base; free(state); }
static void sampler_bind(struct pipe_context *base,enum pipe_shader_type shader,unsigned start,unsigned count,void **states)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if (shader!=PIPE_SHADER_FRAGMENT || start || count>1) { reject(c,ENOTSUP); return; }
    if (ps2_render_flush(c->screen->render)) { reject(c,errno); return; }
    c->has_sampler=count && states && states[0];
    if (c->has_sampler) c->sampler=*(struct pipe_sampler_state*)states[0];
}
static void sampler_views(struct pipe_context *base,enum pipe_shader_type shader,unsigned start,
    unsigned count,unsigned trailing,bool take_ownership,struct pipe_sampler_view **views)
{
    struct ps2_pipe_context *c=(void*)base;
    /* Failed binding still consumes supplied ownership, as Gallium requires. */
    if (ready(c) || shader!=PIPE_SHADER_FRAGMENT || start || count>1 || trailing>1 || count+trailing>1 ||
        (count && views && views[0] && views[0]->context!=base)) {
        reject(c,ENOTSUP);
        if (take_ownership && views) for(unsigned i=0;i<count;++i) {
            struct pipe_sampler_view *view=views[i]; pipe_sampler_view_reference(&view,NULL);
        }
        return;
    }
    if (ps2_render_flush(c->screen->render)) { reject(c,errno); }
    struct pipe_sampler_view *new_view=count && views ? views[0] : NULL;
    if (!count && !trailing) return;
    if (take_ownership) { pipe_sampler_view_reference(&c->view,NULL); c->view=new_view; }
    else pipe_sampler_view_reference(&c->view,new_view);
}
static void clear(struct pipe_context *base,unsigned buffers,const struct pipe_scissor_state *scissor,
    const union pipe_color_union *color,double depth,unsigned stencil)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if ((buffers&~(PIPE_CLEAR_COLOR0|PIPE_CLEAR_DEPTH)) || stencil ||
        ((buffers&PIPE_CLEAR_DEPTH) && depth!=1.0) || ((buffers&PIPE_CLEAR_COLOR0) && !color)) {
        reject(c,ENOTSUP); return;
    }
    if (!buffers) return;
    if (bind_target(c) || ps2_render_target_clear(&c->target,scissor,
        buffers&PIPE_CLEAR_COLOR0 ? color : NULL,!!(buffers&PIPE_CLEAR_DEPTH))) reject(c,errno);
}
static void flush(struct pipe_context *base,struct pipe_fence_handle **fence,unsigned flags)
{
    struct ps2_pipe_context *c=(void*)base;
    /* Submission is synchronous; no fake externally shareable fence objects. */
    if (fence) *fence=NULL;
    if (ready(c)) return;
    if (flags) { reject(c,ENOTSUP); return; }
    if (ps2_render_flush(c->screen->render)) reject(c,errno);
}
static void subdata(struct pipe_context *base,struct pipe_resource *r,unsigned level,unsigned usage,
    const struct pipe_box *box,const void *pixels,unsigned stride,uintptr_t layer_stride)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if (!r || r->screen!=base->screen || r->format==PIPE_FORMAT_Z16_UNORM || level ||
        (usage&~(PIPE_MAP_WRITE|PIPE_MAP_DISCARD_RANGE|PIPE_MAP_DISCARD_WHOLE_RESOURCE)) ||
        !box || box->x<0 || box->y<0 || box->z || box->depth!=1 || box->width<=0 || box->height<=0 ||
        (unsigned)box->x>=r->width0 || (unsigned)box->y>=r->height0 ||
        (unsigned)box->width>r->width0-(unsigned)box->x || (unsigned)box->height>r->height0-(unsigned)box->y || !pixels) {
        reject(c,EINVAL); return;
    }
    size_t row=(size_t)box->width*(r->format==PIPE_FORMAT_R8G8B8X8_UNORM ? 4 : 2);
    if (stride<row || (size_t)(box->height-1)>(SIZE_MAX-row)/stride) { reject(c,EINVAL); return; }
    size_t bytes=(size_t)(box->height-1)*stride+row;
    if (layer_stride && layer_stride<bytes) { reject(c,EINVAL); return; }
    if (ps2_sampler_upload(c->screen->render,r,box,pixels,bytes,stride)) reject(c,errno);
}
static void resource_copy(struct pipe_context *base,struct pipe_resource *dst,unsigned level,unsigned dx,unsigned dy,unsigned dz,
    struct pipe_resource *src,unsigned src_level,const struct pipe_box *box)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if (!src || !dst || !box) { reject(c,EINVAL); return; }
    if (src->screen!=base->screen || dst->screen!=base->screen ||
        src->target!=PIPE_TEXTURE_2D || dst->target!=PIPE_TEXTURE_2D ||
        src->format!=dst->format ||
        (src->format!=PIPE_FORMAT_R8G8B8X8_UNORM && src->format!=PIPE_FORMAT_R5G5B5X1_UNORM) ||
        level || src_level || dz || box->z || box->depth!=1) {
        reject(c,ENOTSUP); return;
    }
    struct ps2_resource *s=(void*)src,*d=(void*)dst;
    if (s->owner!=c->screen->render->gs || d->owner!=s->owner) { reject(c,EXDEV); return; }
    /* Check signed box coordinates and unsigned destinations before narrowing
     * to the native batch ABI. A rejected copy must not submit pending draws. */
    if (box->x<0 || box->y<0 || box->width<=0 || box->height<=0 ||
        (uint64_t)box->x+box->width>src->width0 ||
        (uint64_t)box->y+box->height>src->height0 ||
        (uint64_t)dx+box->width>dst->width0 ||
        (uint64_t)dy+box->height>dst->height0 || dx>INT32_MAX || dy>INT32_MAX) {
        reject(c,EINVAL); return;
    }
    const struct ps2_pipe_copy_box copy={box->x,box->y,(int32_t)dx,(int32_t)dy,box->width,box->height};
    /* The batch handles fencing, odd-width scratch and overlap ordering while
     * preserving the live framebuffer, sampler and resource references. */
    if (ps2_pipe_copy_boxes(base,s->handle,d->handle,&copy,1)) reject(c,errno);
}
static void flush_resource(struct pipe_context *base,struct pipe_resource *r)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if (!r || r->screen!=base->screen) { reject(c,EXDEV); return; }
    if (ps2_render_flush(c->screen->render)) reject(c,errno);
}
static void blit(struct pipe_context *base,const struct pipe_blit_info *info)
{
    struct ps2_pipe_context *c=(void*)base;
    if (ready(c)) return;
    if (!info || !info->src.resource || !info->dst.resource) { reject(c,EINVAL); return; }
    struct pipe_resource *src=info->src.resource,*dst=info->dst.resource;
    if (src->screen!=base->screen || dst->screen!=base->screen ||
        src->target!=PIPE_TEXTURE_2D || dst->target!=PIPE_TEXTURE_2D || src->format!=dst->format ||
        (src->format!=PIPE_FORMAT_R8G8B8X8_UNORM && src->format!=PIPE_FORMAT_R5G5B5X1_UNORM) ||
        info->src.format!=src->format || info->dst.format!=dst->format ||
        info->src.level || info->dst.level || info->src.box.z || info->dst.box.z ||
        info->src.box.depth!=1 || info->dst.box.depth!=1 ||
        info->filter!=PIPE_TEX_FILTER_NEAREST || (info->mask & ~PIPE_MASK_RGBA) ||
        ((info->mask & (PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B)) && (info->mask & (PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B))!=(PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B)) ||
        info->alpha_blend || info->num_window_rectangles || info->dst_sample ||
        src->nr_samples>1 || dst->nr_samples>1 || src->nr_storage_samples>1 || dst->nr_storage_samples>1) {
        reject(c,ENOTSUP); return;
    }
    struct ps2_resource *s=(void*)src,*d=(void*)dst;
    if (s->owner!=c->screen->render->gs || d->owner!=s->owner) { reject(c,EXDEV); return; }
    const struct ps2_surface *native_s=ps2_surface_lookup(s->owner,s->handle),
        *native_d=ps2_surface_lookup(d->owner,d->handle);
    unsigned bpp=src->format==PIPE_FORMAT_R8G8B8X8_UNORM ? 32 : 16;
    if (!native_s || !native_d || native_s->width!=src->width0 || native_s->height!=src->height0 ||
        native_d->width!=dst->width0 || native_d->height!=dst->height0 ||
        native_s->bits_per_pixel!=bpp || native_d->bits_per_pixel!=bpp) { reject(c,EINVAL); return; }
    const struct ps2_pipe_blit_rect box={info->src.box.x,info->src.box.y,
        info->src.box.width,info->src.box.height,info->dst.box.x,info->dst.box.y,
        info->dst.box.width,info->dst.box.height,info->scissor_enable,
        info->scissor.minx,info->scissor.miny,info->scissor.maxx,info->scissor.maxy,
        !(info->mask & (PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B))};
    if (ps2_pipe_blit_rect(base,s->handle,d->handle,&box)) reject(c,errno);
}
static void draw_unsupported(struct pipe_context *base,const struct pipe_draw_info *info,unsigned drawid,
    const struct pipe_draw_indirect_info *indirect,const struct pipe_draw_start_count_bias *draws,unsigned count)
{ (void)info; (void)drawid; (void)indirect; (void)draws; (void)count; reject((void*)base,ENOTSUP); }
int ps2_pipe_triangle(struct pipe_context *base,const float position[3][4],const float color[3][4],
    const float uv[3][2],int depth_test)
{
    if (!valid_context(base)) { errno=EINVAL; return -1; }
    struct ps2_pipe_context *c=(void*)base;
    if (bind_target(c)) return -1;
    int status;
    if (uv) {
        if (!c->view || !c->has_sampler) return reject(c,ENOTSUP);
        status=ps2_sampler_triangle(&c->target,c->view,&c->sampler,position,uv,color,1,depth_test);
    } else status=ps2_render_target_triangle(&c->target,position,color,depth_test);
    return status ? reject(c,errno) : 0;
}
static void context_destroy(struct pipe_context *base)
{
    struct ps2_pipe_context *c=(void*)base;
    if (c->closing) return;
    c->closing=1;
    ps2_render_target_cleanup(&c->target);
    pipe_surface_reference(&c->color,NULL); pipe_surface_reference(&c->depth,NULL);
    pipe_sampler_view_reference(&c->view,NULL);
    context_unref(c);
}
static struct pipe_context *context_create(struct pipe_screen *base,void *private,unsigned flags)
{
    struct ps2_pipe_screen *s=(void*)base;
    if (s->closing || s->context || flags || s->render->gs->error) { errno=ENOTSUP; return NULL; }
    struct ps2_pipe_context *c=calloc(1,sizeof(*c));
    if (!c) return NULL;
    c->base.screen=base; c->base.priv=private; c->screen=s; c->refs=1;
    if (ps2_render_target_init(&c->target,base,s->render)) { free(c); return NULL; }
    c->base.destroy=context_destroy;
    c->base.create_surface=surface_create; c->base.surface_destroy=surface_destroy;
    c->base.set_framebuffer_state=framebuffer;
    c->base.create_sampler_view=view_create; c->base.sampler_view_destroy=view_destroy;
    c->base.create_sampler_state=sampler_create; c->base.delete_sampler_state=sampler_delete;
    c->base.bind_sampler_states=sampler_bind; c->base.set_sampler_views=sampler_views;
    c->base.clear=clear; c->base.flush=flush; c->base.flush_resource=flush_resource;
    c->base.texture_subdata=subdata; c->base.resource_copy_region=resource_copy;
    c->base.blit=blit;
    c->base.draw_vbo=draw_unsupported;
    ++s->refs; s->context=c;
    return &c->base;
}
static struct ps2_pipe_screen *screen_create(struct ps2_render_client *render)
{
    if (!render || !render->gs || render->gs->error || render->gs->fd<0 ||
        !(render->gs->caps.features&PS2GS_HW_OWNED_RENDER) ||
        !(render->gs->caps.features&PS2GS_HW_TEXTURE)) { errno=ENOTSUP; return NULL; }
    struct ps2_pipe_screen *s=calloc(1,sizeof(*s));
    if (!s) return NULL;
    s->refs=1; s->render=render;
    s->base.destroy=screen_destroy; s->base.get_name=name; s->base.get_vendor=vendor;
    s->base.get_device_vendor=vendor; s->base.get_param=parameter; s->base.get_paramf=parameterf;
    s->base.get_shader_param=shader_parameter; s->base.is_format_supported=format_supported;
    s->base.context_create=context_create; s->base.resource_create=resource_create; s->base.resource_destroy=resource_destroy;
    return s;
}
struct pipe_screen *ps2_pipe_screen_create_shared(struct ps2_render_client *render)
{ struct ps2_pipe_screen *s=screen_create(render); return s ? &s->base : NULL; }
struct pipe_screen *ps2_pipe_screen_create(const char *device)
{
    struct ps2_surface_client gs={.fd=-1}; struct ps2_render_client render;
    if (!device || ps2_surface_open(&gs,device)) return NULL;
    if (ps2_render_init(&render,&gs)) { ps2_surface_close(&gs); return NULL; }
    struct ps2_pipe_screen *s=screen_create(&render);
    if (!s) { ps2_surface_close(&gs); return NULL; }
    s->gs=gs; s->own_render=render; s->own_render.gs=&s->gs;
    s->render=&s->own_render; s->owns_file=1;
    return &s->base;
}
