/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"
#include "ps2_render_raster.h"
static int raster_fail;
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list args;va_start(args,op);void *data=va_arg(args,void*);va_end(args);
 if(raster_fail){errno=EIO;return -1;}
 if(op==PS2GS_TEXTURE_TRANSFER){
  const struct ps2gs_texture_transfer *in=data;
  const struct ps2gs_texture_triangle *tri=(void*)(uintptr_t)in->draw.triangles;
  assert(!(in->draw.count&1));
  for(unsigned i=0;i<in->draw.count;i+=2){
   struct ps2gs_texture_transfer part=*in;part.draw.count=2;part.draw.triangles=(uintptr_t)(tri+i);
   if(composite_ioctl(fd,op,&part))return -1;
  }
  return 0;
 }
 return composite_ioctl(fd,op,data);
}
#define FIX(v) ((pixman_fixed_t)((v)*65536))
static const pixman_trapezoid_t shapes[]={
 {FIX(-2.375),FIX(22.875),{{FIX(-4.25),FIX(-3)},{FIX(11.5),FIX(26)}},{{FIX(21.75),FIX(-3)},{FIX(17.125),FIX(26)}}},
 {FIX(3.3125),FIX(19.6875),{{FIX(6.625),FIX(3.3125)},{FIX(8.125),FIX(19.6875)}},{{FIX(14.125),FIX(3.3125)},{FIX(12.875),FIX(19.6875)}}},
 {FIX(5),FIX(5),{{0,0},{0,FIX(1)}},{{FIX(19),0},{FIX(19),FIX(1)}}},
 {FIX(15.125),FIX(24.25),{{FIX(14.75),FIX(15.125)},{FIX(3.125),FIX(24.25)}},{{FIX(23),FIX(15.125)},{FIX(18),FIX(24.25)}}}
};
static unsigned reverse_byte(unsigned v)
{v=((v&0x55)<<1)|((v>>1)&0x55);v=((v&0x33)<<2)|((v>>2)&0x33);return (v<<4)|(v>>4);}
static void coverage_compare(const char *name,unsigned depth,unsigned msb,const unsigned char *raw,const unsigned char *ref)
{
 for(unsigned y=0;y<23;++y)for(unsigned x=0;x<19;++x){
  unsigned a=depth==1?(raw[y*24+x/8]>>(msb?7-(x&7):x&7))&1:raw[y*24+x]&((1U<<depth)-1);
  unsigned b=depth==1?(ref[y*24+x/8]>>(x&7))&1:depth==4?(ref[y*24+x/2]>>((x&1)*4))&15:ref[y*24+x];
  if(a!=b){fprintf(stderr,"coverage mismatch %s (%u,%u): GS=%u pixman=%u\n",name,x,y,a,b);abort();}
 }
}
static unsigned random_state=0x19731961;
static unsigned next_random(void){random_state=random_state*1664525U+1013904223U;return random_state;}
static void test_shapes(const char *name)
{
 unsigned depth=strstr(name,"a1")?1:strstr(name,"a4")?4:8,msb=strstr(name,"msb")!=NULL;
 enum {W=19,H=23,STRIDE=24};
 unsigned char raw[H*STRIDE],initial[H*STRIDE],ref[H*STRIDE];
 memset(raw,0x96,sizeof(raw));memset(ref,0,sizeof(ref));
 for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
  unsigned value=(x*7+y*13)%((1U<<depth));
  if(depth==1){unsigned shift=msb?7-(x&7):x&7;
   raw[y*STRIDE+x/8]=(raw[y*STRIDE+x/8]&~(1<<shift))|(value<<shift);
   ref[y*STRIDE+x/8]|=value<<(x&7);
  }else{
   raw[y*STRIDE+x]=depth==4?0xa0|value:value;
   if(depth==4)ref[y*STRIDE+x/2]|=value<<((x&1)*4);
   else ref[y*STRIDE+x]=value;
  }
 }
 memcpy(initial,raw,sizeof(raw));
 struct ps2_raster_target target={raw,sizeof(raw),STRIDE,W,H,depth,msb};
 pixman_image_t *image=pixman_image_create_bits(depth==1?PIXMAN_a1:depth==4?PIXMAN_a4:PIXMAN_a8,W,H,(uint32_t*)ref,STRIDE);assert(image);
 rgba_enabled=1;polygon_setup(1,32);
 int dx=-1,dy=2;
 if(!strncmp(name,"random",6)){
  unsigned char seed_ref[sizeof(ref)];memcpy(seed_ref,ref,sizeof(ref));
  for(unsigned i=0;i<32;++i){
   memcpy(raw,initial,sizeof(raw));memcpy(ref,seed_ref,sizeof(ref));
   int32_t t=(int32_t)(next_random()%(26*65536))-3*65536,b=t+1+next_random()%(9*65536);
   int32_t l=(int32_t)(next_random()%(25*65536))-3*65536,r=l+1+next_random()%(7*65536),
    l2=l+(int32_t)(next_random()%(9*65536))-4*65536,r2=l2+1+next_random()%(7*65536);
   pixman_trapezoid_t shape={.top=t,.bottom=b,.left={{l,t},{l2,b}},.right={{r,t},{r2,b}}};
   pixman_rasterize_trapezoid(image,&shape,0,0);
   assert(!ps2_pipe_raster_trapezoids(ctx,&target,0,0,&shape,1));
   coverage_compare(name,depth,msb,raw,ref);
  }
 }else if(!strncmp(name,"rectangle",9)){
  pixman_trapezoid_t shape={.top=0,.bottom=H*65536,.left={{0,0},{0,H*65536}},.right={{W*65536,0},{W*65536,H*65536}}};
  pixman_rasterize_trapezoid(image,&shape,0,0);
  assert(!ps2_pipe_raster_trapezoids(ctx,&target,0,0,&shape,1));
 }else if(!strncmp(name,"triangles",9)){
  pixman_triangle_t tris[]={
   {{FIX(-3.5),FIX(2.125)},{FIX(18.625),FIX(-2.375)},{FIX(13.125),FIX(21.875)}},
   {{FIX(11.125),FIX(4.5)},{FIX(4.25),FIX(19.25)},{FIX(22.75),FIX(19.25)}},
   {{FIX(7),FIX(7)},{FIX(7),FIX(7)},{FIX(7),FIX(7)}}};
  pixman_add_triangles(image,dx,dy,3,tris);
  assert(!ps2_pipe_raster_triangles(ctx,&target,dx,dy,tris,3));
  pixman_point_fixed_t t=tris[0].p2;tris[0].p2=tris[0].p3;tris[0].p3=t;
  pixman_add_triangles(image,0,0,1,tris);
  assert(!ps2_pipe_raster_triangles(ctx,&target,0,0,tris,1));
 }else if(!strncmp(name,"traps",5)){
  pixman_trap_t traps[]={
   {.top={.y=FIX(-2.375),.l=FIX(-4.25),.r=FIX(21.75)},.bot={.y=FIX(22.875),.l=FIX(11.5),.r=FIX(17.125)}},
   {.top={.y=FIX(3.3125),.l=FIX(6.625),.r=FIX(14.125)},.bot={.y=FIX(19.6875),.l=FIX(8.125),.r=FIX(12.875)}}};
  pixman_add_traps(image,dx,dy,2,traps);
  assert(!ps2_pipe_raster_traps(ctx,&target,dx,dy,traps,2));
 }else{
  for(unsigned i=0;i<4;++i)pixman_rasterize_trapezoid(image,shapes+i,dx,dy);
  assert(!ps2_pipe_raster_trapezoids(ctx,&target,dx,dy,shapes,4));
 }
 for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
  unsigned a=depth==1?(raw[y*STRIDE+x/8]>>(msb?7-(x&7):x&7))&1:raw[y*STRIDE+x]&((1U<<depth)-1);
  unsigned b=depth==1?(ref[y*STRIDE+x/8]>>(x&7))&1:depth==4?(ref[y*STRIDE+x/2]>>((x&1)*4))&15:ref[y*STRIDE+x];
  if(a!=b){fprintf(stderr,"coverage mismatch %s (%u,%u): GS=%u pixman=%u\n",name,x,y,a,b);abort();}
  if(depth==4)assert((raw[y*STRIDE+x]&0xf0)==0xa0);
 }
 unsigned used=depth==1?(W+7)/8:W;
 for(unsigned y=0;y<H;++y){
  assert(!memcmp(raw+y*STRIDE+used,initial+y*STRIDE+used,STRIDE-used));
  if(depth==1){unsigned mask=msb?0x1f:0xf8;assert((raw[y*STRIDE+2]&mask)==(initial[y*STRIDE+2]&mask));}
 }
 pixman_image_unref(image);polygon_finish();
 for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
}
static void raster_guards(void)
{
 rgba_enabled=1;polygon_setup(1,32);unsigned char pixels[32]={0},saved[32]={0};
 struct ps2_raster_target d={pixels,32,4,3,3,8,0};
 unsigned before=transfer_calls;
 assert(ps2_pipe_raster_trapezoids(ctx,&d,INT_MAX,0,shapes,1)<0 && errno==EOVERFLOW);
 assert(!memcmp(pixels,saved,32) && before==transfer_calls);
 d.bytes=1;assert(ps2_pipe_raster_trapezoids(ctx,&d,0,0,shapes,1)<0 && errno==EINVAL);d.bytes=32;
 d.depth=3;assert(ps2_pipe_raster_trapezoids(ctx,&d,0,0,shapes,1)<0 && errno==EINVAL);d.depth=8;
 assert(!ps2_pipe_raster_trapezoids(ctx,&d,0,0,NULL,0) && before==transfer_calls);
 assert(ps2_pipe_raster_trapezoids(ctx,&d,0,0,NULL,1)<0 && errno==EINVAL);
 assert(ps2_pipe_raster_triangles(ctx,&d,0,0,NULL,4097)<0 && errno==EINVAL);
 pixman_trapezoid_t *many=calloc(258,sizeof(*many));assert(many);
 for(unsigned i=0;i<258;++i)many[i]=(pixman_trapezoid_t){.top=0,.bottom=3*65536,
   .left={{FIX(1.2),0},{FIX(1.2),3*65536}},.right={{FIX(1.8),0},{FIX(1.8),3*65536}}};
 assert(ps2_pipe_raster_trapezoids(ctx,&d,0,0,many,258)<0 && errno==E2BIG);
 assert(!memcmp(pixels,saved,32) && before==transfer_calls);free(many);
 struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;unsigned caps=gs->caps.features;gs->caps.features&=~PS2GS_HW_TEXTURE_TRANSFER;
 assert(ps2_pipe_raster_trapezoids(ctx,&d,0,0,shapes,1)<0 && errno==ENOTSUP && !gs->error);gs->caps.features=caps;
 polygon_finish();
}
int main(int argc,char **argv)
{
 assert(argc==2);
 if(!strcmp(argv[1],"guards"))raster_guards();
 else if(!strcmp(argv[1],"failure")){
  rgba_enabled=1;polygon_setup(1,32);unsigned char pixels[32]={0};
  struct ps2_raster_target d={pixels,32,4,3,3,8,0};raster_fail=1;
  assert(ps2_pipe_raster_trapezoids(ctx,&d,0,0,shapes,1)<0 && errno==EIO);
  assert(ps2_pipe_context_render(ctx)->gs->error==EIO);raster_fail=0;polygon_finish();
 }else test_shapes(argv[1]);
 puts("GS raster coverage case PASS");return 0;
}
