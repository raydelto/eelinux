/* SPDX-License-Identifier: MIT */
#include "previous-pipe.c"
#include "ps2_transform.h"
#include "ps2_copy_exa.h"
#include <float.h>
#include <math.h>
static struct ps2gs_command observed[4096];
static unsigned observed_count,submit_attempts,fail_submit;
static int malloc_fail;
static struct ps2_transform_vertex *change_vertices;
static float *change_matrix;
static struct ps2_transform_viewport *change_viewport;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n){if(malloc_fail){errno=ENOMEM;return NULL;}return __real_malloc(n);}
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
 if(op==PS2GS_RENDER_SUBMIT){
  if(++submit_attempts==fail_submit){errno=EIO;return -1;}
  if(change_vertices){change_vertices[0].position[0]=NAN;change_vertices=NULL;change_matrix[0]=NAN;
   change_matrix=NULL;change_viewport->width=0;change_viewport=NULL;}
  const struct ps2gs_render_submit *s=arg;const struct ps2gs_command *commands=(void*)(uintptr_t)s->commands;
  for(unsigned i=0;i<s->count;++i)if(commands[i].op==PS2GS_TRIANGLE){assert(observed_count<4096);observed[observed_count++]=commands[i];}
 }
 return pipe_ioctl(fd,op,arg);
}
static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static const struct ps2_transform_viewport viewport={0,0,128,96,0,1};
static struct ps2_transform_vertex vertices[768];
static void transform_setup(void)
{
 setup(1);
 vertices[0]=(struct ps2_transform_vertex){{-.5f,-.5f,0,1},{0,1,1,1}};
 vertices[1]=(struct ps2_transform_vertex){{.5f,-.5f,0,1},{0,1,1,1}};
 vertices[2]=(struct ps2_transform_vertex){{0,.5f,0,1},{0,1,1,1}};
}
static void finish_transform(void){ctx->destroy(ctx);release_external();screen->destroy(screen);assert(closes==1);}
static unsigned draw_transform(unsigned n,const float *m,const struct ps2_transform_viewport *v,int depth)
{unsigned emitted=999;assert(!ps2_pipe_transform_triangles(ctx,vertices,n,m,v,depth,&emitted));return emitted;}
static void check_vertex(struct ps2gs_vertex v,double x,double y,double z,uint32_t colour)
{
 assert(labs((long)v.x-(long)(x*16+.5))<=1 && labs((long)v.y-(long)(y*16+.5))<=1);
 assert(labs((long)v.z-(long)((1-z)*65535+.5))<=1 && v.rgba==colour);
}
static void transform_transform(void)
{transform_setup();assert(draw_transform(3,identity,&viewport,1)==1 && observed_count==1 && submit_attempts==1);
 check_vertex(observed[0].v[0],32,72,.5,0x80ffff00);check_vertex(observed[0].v[1],96,72,.5,0x80ffff00);
 check_vertex(observed[0].v[2],64,24,.5,0x80ffff00);assert(observed[0].flags==PS2GS_DEPTH_TEST);finish_transform();}
static void transform_matrix(void)
{transform_setup();const float m[16]={0,-.5f,0,.125f,.5f,0,0,-.0625f,0,0,.5f,.25f,0,0,0,1};
 assert(draw_transform(3,m,&viewport,0)==1);check_vertex(observed[0].v[0],88,63,.625,0x80ffff00);
 check_vertex(observed[0].v[1],88,39,.625,0x80ffff00);check_vertex(observed[0].v[2],56,51,.625,0x80ffff00);
 assert(!observed[0].flags);finish_transform();}
static void transform_perspective(void)
{transform_setup();float m[16];memcpy(m,identity,sizeof(m));m[14]=.5f;
 vertices[0].position[2]=-.5f;vertices[1].position[2]=.5f;vertices[2].position[2]=.25f;
 assert(draw_transform(3,m,&viewport,1)==1);
 for(unsigned i=0;i<3;++i){double w=1+.5*vertices[i].position[2];
  check_vertex(observed[0].v[i],64*(1+vertices[i].position[0]/w),48*(1-vertices[i].position[1]/w),
   .5*(1+vertices[i].position[2]/w),0x80ffff00);}finish_transform();}
static void transform_clip(void)
{transform_setup();vertices[0].position[0]=-2;vertices[0].position[1]=0;
 vertices[1].position[0]=0;vertices[1].position[1]=1;vertices[2].position[0]=0;vertices[2].position[1]=-1;
 assert(draw_transform(3,identity,&viewport,1)==2 && observed_count==2);
 const unsigned expected[4][2]={{0,24*16},{64*16,0},{64*16,96*16},{0,72*16}};
 for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j){unsigned matched=0;
  for(unsigned k=0;k<4;++k)matched|=observed[i].v[j].x==expected[k][0] && observed[i].v[j].y==expected[k][1];
  assert(matched);}
 finish_transform();}
static void transform_near(void)
{transform_setup();vertices[0].position[2]=-2;assert(draw_transform(3,identity,&viewport,1)==2);
 check_vertex(observed[0].v[0],64,72,0,0x80ffff00);check_vertex(observed[0].v[1],96,72,.5,0x80ffff00);
 check_vertex(observed[1].v[2],48,48,0,0x80ffff00);finish_transform();}
static void transform_eye(void)
{transform_setup();for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)vertices[i].position[j]*=1e-8f;
 assert(!draw_transform(3,identity,&viewport,1) && !submit_attempts);
 for(unsigned i=0;i<3;++i)vertices[i].position[3]=-1;
 assert(!draw_transform(3,identity,&viewport,1) && !submit_attempts);finish_transform();}
static void transform_viewport(void)
{transform_setup();struct ps2_transform_viewport v={8,12,80,48,.25f,.75f};assert(draw_transform(3,identity,&v,1)==1);
 check_vertex(observed[0].v[0],28,48,.5,0x80ffff00);check_vertex(observed[0].v[2],48,24,.5,0x80ffff00);
 v.min_depth=v.max_depth=.75f;assert(draw_transform(3,identity,&v,1)==1);assert(observed[1].v[0].z==16384);finish_transform();}
static void transform_colours(void)
{transform_setup();vertices[0].color[0]=1;vertices[0].color[1]=vertices[0].color[2]=0;
 vertices[1].color[1]=1;vertices[1].color[2]=0;vertices[2].color[1]=0;
 assert(draw_transform(3,identity,&viewport,1)==1);
 assert(observed[0].v[0].rgba==0x800000ff && observed[0].v[1].rgba==0x8000ff00 && observed[0].v[2].rgba==0x80ff0000);
 vertices[0].position[0]=-2;vertices[0].position[1]=0;vertices[1].position[0]=0;vertices[1].position[1]=1;
 vertices[2].position[0]=0;vertices[2].position[1]=-1;assert(draw_transform(3,identity,&viewport,1)==2);
 assert(observed[1].v[0].rgba==0x80008080);finish_transform();}
static void transform_noop(void)
{transform_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));
 assert(!draw_transform(0,identity,&viewport,1));for(unsigned i=0;i<3;++i)vertices[i].position[0]+=4;
 assert(!draw_transform(3,identity,&viewport,1));vertices[0]=vertices[1]=vertices[2];assert(!draw_transform(3,identity,&viewport,1));
 assert(!submit_attempts && r->count==1);finish_transform();}
static void reject_transform(unsigned count,const float *m,const struct ps2_transform_viewport *v)
{unsigned emitted=999;assert(ps2_pipe_transform_triangles(ctx,vertices,count,m,v,1,&emitted)==-1 && !emitted);assert(!ps2_pipe_context_error(ctx));}
static void transform_preflight(void)
{
 transform_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));
 reject_transform(4,identity,&viewport);reject_transform(771,identity,&viewport);float m[16];memcpy(m,identity,sizeof(m));m[15]=NAN;
 reject_transform(3,m,&viewport);struct ps2_transform_viewport v=viewport;v.x=UINT32_MAX;reject_transform(3,identity,&v);
 v=viewport;v.min_depth=.75f;v.max_depth=.25f;reject_transform(3,identity,&v);
 memcpy(vertices+3,vertices,3*sizeof(*vertices));vertices[5].position[0]=INFINITY;reject_transform(6,identity,&viewport);
 vertices[5]=vertices[2];vertices[5].color[3]=.5f;reject_transform(6,identity,&viewport);
 vertices[5].color[3]=1;vertices[5].color[0]=-1;reject_transform(6,identity,&viewport);
 assert(ps2_pipe_transform_triangles(ctx,(void*)(UINTPTR_MAX-2),3,identity,&viewport,1,NULL)==-1);
 assert(!submit_attempts && r->count==1);finish_transform();
}
static void transform_allocation(void)
{transform_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,2,2,0x80444444));malloc_fail=1;
 reject_transform(3,identity,&viewport);assert(errno==ENOMEM && !submit_attempts && r->count==1);malloc_fail=0;finish_transform();}
static void transform_budget(void)
{transform_setup();for(unsigned i=3;i<768;++i)vertices[i]=vertices[i%3];
 assert(draw_transform(768,identity,&viewport,1)==256 && observed_count==256 && submit_attempts==4);
 for(unsigned i=0;i<observed_count;++i)assert(observed[i].flags==PS2GS_DEPTH_TEST);
 finish_transform();}
static void transform_snapshot(void)
{transform_setup();float m[16];memcpy(m,identity,sizeof(m));struct ps2_transform_viewport v=viewport;
 struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_bind(r,0,0));assert(!ps2_render_fill(r,0,0,2,2,0x80444444));
 change_vertices=vertices;change_matrix=m;change_viewport=&v;assert(draw_transform(3,m,&v,1)==1 && submit_attempts==2);
 assert(isnan(vertices[0].position[0]) && isnan(m[0]) && !v.width);check_vertex(observed[0].v[0],32,72,.5,0x80ffff00);finish_transform();}
static void transform_target(void)
{transform_setup();texture_setup();int32_t refs=texture_view->reference.count;struct ps2_render_client *r=ps2_pipe_context_render(ctx);
 assert(!ps2_render_bind(r,0,0));assert(draw_transform(3,identity,&viewport,1)==1);
 assert(r->target==((struct ps2_resource*)color_resource)->handle && r->depth && texture_view->reference.count==refs);
 assert(color_resource->reference.count==3 && depth_resource->reference.count==3);finish_transform();}
static void transform_xorg(void)
{
 transform_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);void *privates[16]={0};
 ScreenRec xs={.GetScreenPixmap=get_front,.devPrivates=(void*)privates};uint32_t handle=((struct ps2_resource*)color_resource)->handle;
 PixmapRec fp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=720,.height=576}};
 PixmapRec cp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=128,.height=96},.devPrivates=(void*)&handle};front_pixmap=&fp;
 struct ps2_surface_exa exa={0};struct ps2_exa_context_bridge bridge={0};struct ps2_copy_exa copy={0};
 assert(ps2_surface_exa_prepare(&xs,&exa,r->gs) && ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
 assert(ps2_copy_exa_prepare(&xs,&copy,&exa,ctx));assert(draw_transform(3,identity,&viewport,1)==1);
 assert(!ps2_pipe_triangle(ctx,position,colors,NULL,1));unsigned before=submit_attempts;
 assert(exa.driver.PrepareCopy(&cp,&fp,1,1,GXcopy,0xffffff));exa.driver.Copy(&fp,0,0,32,64,128,96);exa.driver.DoneCopy(&fp);
 assert(submit_attempts==before+1 && last_copy.source==handle && last_copy.destination==0 && last_copy.width==128);
 assert(draw_transform(3,identity,&viewport,1)==1);ps2_copy_exa_cleanup(&xs,&copy);ps2_exa_context_bridge_cleanup(&bridge);finish_transform();
}
static void transform_failure(void)
{transform_setup();for(unsigned i=3;i<768;++i)vertices[i]=vertices[i%3];fail_submit=2;unsigned emitted=999;
 assert(ps2_pipe_transform_triangles(ctx,vertices,768,identity,&viewport,1,&emitted)==-1 && !emitted);
 assert(submit_attempts==2 && observed_count==64 && ps2_pipe_context_error(ctx)==EIO);
 assert(ps2_pipe_transform_triangles(ctx,vertices,3,identity,&viewport,1,NULL)==-1 && submit_attempts==2);finish_transform();}
static void transform_rgb555(void)
{
 init();gs.caps.bits_per_pixel=16;screen=ps2_pipe_screen_create_shared(&render);assert(screen);
 ctx=screen->context_create(screen,NULL,0);assert(ctx);
 struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R5G5B5X1_UNORM,.width0=128,.height0=96,
  .depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
 color_resource=screen->resource_create(screen,&t);assert(color_resource);t.format=PIPE_FORMAT_Z16_UNORM;t.bind=PIPE_BIND_DEPTH_STENCIL;
 depth_resource=screen->resource_create(screen,&t);assert(depth_resource);struct pipe_surface st={.format=color_resource->format};
 color_surface=ctx->create_surface(ctx,color_resource,&st);st.format=depth_resource->format;depth_surface=ctx->create_surface(ctx,depth_resource,&st);
 assert(color_surface && depth_surface);struct pipe_framebuffer_state fb={.width=128,.height=96,.nr_cbufs=1,.cbufs={color_surface},.zsbuf=depth_surface};
 ctx->set_framebuffer_state(ctx,&fb);assert(!ps2_pipe_context_error(ctx));
 vertices[0]=(struct ps2_transform_vertex){{-.5f,-.5f,0,1},{0,1,1,1}};
 vertices[1]=(struct ps2_transform_vertex){{.5f,-.5f,0,1},{0,1,1,1}};
 vertices[2]=(struct ps2_transform_vertex){{0,.5f,0,1},{0,1,1,1}};
 assert(draw_transform(3,identity,&viewport,1)==1 && last_submit.bits_per_pixel==16);
 check_vertex(observed[0].v[0],32,72,.5,0x80ffff00);ctx->destroy(ctx);release_external();screen->destroy(screen);ps2_surface_close(&gs);assert(closes==1);
}
static void transform_lifetime(void)
{transform_setup();assert(!ps2_pipe_context_acquire(ctx));ctx->destroy(ctx);
 assert(ps2_pipe_transform_triangles(ctx,vertices,3,identity,&viewport,1,NULL)==-1 && errno==ESHUTDOWN);
 release_external();screen->destroy(screen);assert(!closes);ps2_pipe_context_release(ctx);assert(closes==1);}
static void transform_random(void)
{
 transform_setup();uint32_t state=736;
 for(unsigned k=0;k<160;++k){for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){state=state*1664525U+1013904223U;
   vertices[i].position[j]=((int)(state%1025)-512)/128.0f;}unsigned before=observed_count;
  assert(draw_transform(3,identity,&viewport,1)<=8);
  for(unsigned i=before;i<observed_count;++i)for(unsigned j=0;j<3;++j)
   assert(observed[i].v[j].x<=128*16 && observed[i].v[j].y<=96*16 && observed[i].v[j].z<=65535 && observed[i].v[j].rgba==0x80ffff00);
 }finish_transform();
}
int main(int argc,char **argv)
{
 assert(argc==2);
#define CASE(name) if(!strcmp(argv[1],#name))transform_##name();else
 CASE(transform) CASE(matrix) CASE(perspective) CASE(clip) CASE(near) CASE(eye) CASE(viewport) CASE(colours) CASE(noop)
 CASE(preflight) CASE(allocation) CASE(budget) CASE(snapshot) CASE(target) CASE(xorg) CASE(failure) CASE(rgb555) CASE(lifetime) CASE(random) assert(0);
 puts("native transformed triangle case PASS");return 0;
}
