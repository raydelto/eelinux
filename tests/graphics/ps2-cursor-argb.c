/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"
static int argb_fail_ioctl;
int __wrap_ioctl(int fd,unsigned long op,...)
{
 va_list args;va_start(args,op);void *data=va_arg(args,void*);va_end(args);
 if(argb_fail_ioctl){errno=EIO;return -1;}
 return composite_ioctl(fd,op,data);
}
static uint32_t cursor_argb[64*64],cursor_original[64*64];
static void argb_seed(unsigned width,unsigned height,unsigned salt)
{
 static const unsigned alpha[]={0,1,64,128,254,255};
 for(unsigned i=0;i<width*height;++i){unsigned a=alpha[(i+salt)%6];
  cursor_argb[i]=(a<<24)|((((i*19+salt*13)&255)*a+127)/255<<16)|
   ((((i*37+23)&255)*a+127)/255<<8)|(((i*59+91)&255)*a+127)/255;
 }
 memcpy(cursor_original,cursor_argb,sizeof(cursor_argb));
}
static void argb_golden(unsigned bpp,unsigned width,unsigned height,unsigned xhot,unsigned yhot,int x,int y,int visible)
{
 unsigned cpp=bpp/8,stride=720*cpp;unsigned char *memory=calloc(576,stride);assert(memory);
 for(unsigned i=0;i<720*576;++i)memcpy(memory+i*cpp,background_pixels+i,cpp);
 pixman_image_t *dst=pixman_image_create_bits(bpp==16?PIXMAN_x1b5g5r5:PIXMAN_x8b8g8r8,720,576,(uint32_t*)memory,stride);
 pixman_image_t *src=pixman_image_create_bits(PIXMAN_a8r8g8b8,width,height,cursor_original,width*4);assert(src && dst);
 if(visible && (int64_t)x-xhot<720 && (int64_t)x-xhot+(int64_t)width>0 &&
    (int64_t)y-yhot<576 && (int64_t)y-yhot+(int64_t)height>0)
  pixman_image_composite32(PIXMAN_OP_OVER,src,NULL,dst,0,0,0,0,x-(int)xhot,y-(int)yhot,width,height);
 uint32_t bits=bpp==16?0x7fff:0xffffff;
 for(unsigned i=0;i<720*576;++i){uint32_t wanted=0;memcpy(&wanted,memory+i*cpp,cpp);
  uint32_t observed=model(0)->data[i];
  if(visible){if((wanted&bits)!=(observed&bits)){fprintf(stderr,"ARGB cursor mismatch bpp=%u x=%u y=%u expected=%x actual=%x\n",bpp,i%720,i/720,wanted,observed);abort();}}
  else assert(observed==background_pixels[i]);
 }
 pixman_image_unref(src);pixman_image_unref(dst);free(memory);
}
static void argb_case(const char *name)
{
 unsigned bpp=strstr(name,"rgb555")?16:32;cursor_setup(bpp);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);
 struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
 argb_seed(11,9,0);assert(!ps2_pipe_cursor_set_argb(cursor,cursor_argb,11*9*4,11,9,3,2,90,80));
 argb_golden(bpp,11,9,3,2,90,80,1);
 if(!strncmp(name,"snapshot",8))memset(cursor_argb,0,sizeof(cursor_argb));
 int positions[][2]={{0,0},{719,575},{-20,12},{730,580},{INT_MAX,INT_MIN},{180,90}};
 for(unsigned i=0;i<6;++i){assert(!ps2_pipe_cursor_move(cursor,positions[i][0],positions[i][1]));argb_golden(bpp,11,9,3,2,positions[i][0],positions[i][1],1);}
 if(!strncmp(name,"damage",6)){
  assert(!ps2_surface_fill(gs,0,176,86,8,8,0x80201040));memcpy(background_pixels,model(0)->data,sizeof(background_pixels));
  assert(!ps2_pipe_cursor_flush(cursor));argb_golden(bpp,11,9,3,2,180,90,1);
 }
 if(!strncmp(name,"suspend",7)){
  assert(!ps2_pipe_cursor_suspend(cursor,1));argb_golden(bpp,11,9,3,2,180,90,0);
  assert(!ps2_pipe_cursor_suspend(cursor,0));argb_golden(bpp,11,9,3,2,180,90,1);
 }
 if(!strncmp(name,"replace",7)){
  argb_seed(7,5,3);assert(!ps2_pipe_cursor_set_argb(cursor,cursor_argb,7*5*4,7,5,6,4,120,130));argb_golden(bpp,7,5,6,4,120,130,1);
  struct ps2_cursor_image im=image(bpp,0);assert(!ps2_pipe_cursor_set(cursor,&im,110,120));golden(&im,110,120,1);
  assert(!ps2_pipe_cursor_set_argb(cursor,cursor_argb,7*5*4,7,5,6,4,120,130));argb_golden(bpp,7,5,6,4,120,130,1);
 }
 assert(!ps2_pipe_cursor_set(cursor,NULL,0,0));argb_golden(bpp,11,9,3,2,0,0,0);finish();
}
static void argb_guards(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);argb_seed(11,9,0);
 unsigned before=composite_sampled;
 assert(ps2_pipe_cursor_set_argb(cursor,NULL,11*9*4,11,9,3,2,90,80)<0 && errno==EINVAL);
 assert(ps2_pipe_cursor_set_argb(cursor,cursor_argb,11*9*4-1,11,9,3,2,90,80)<0 && errno==EINVAL);
 assert(ps2_pipe_cursor_set_argb(cursor,cursor_argb,sizeof(cursor_argb),65,9,3,2,90,80)<0 && errno==EINVAL);
 assert(ps2_pipe_cursor_set_argb(cursor,cursor_argb,sizeof(cursor_argb),11,0,3,2,90,80)<0 && errno==EINVAL);
 assert(ps2_pipe_cursor_set_argb(cursor,cursor_argb,sizeof(cursor_argb),11,9,11,2,90,80)<0 && errno==EINVAL);
 assert(ps2_pipe_cursor_set_argb(cursor,(void*)(UINTPTR_MAX-8),11*9*4,11,9,3,2,90,80)<0 && errno==EINVAL);
 assert(composite_sampled==before && !ps2_pipe_context_render(ctx)->gs->error);
 struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;unsigned caps=gs->caps.features;
 gs->caps.features&=~PS2GS_HW_CHANNEL_MAP_BLIT;
 assert(ps2_pipe_cursor_set_argb(cursor,cursor_argb,11*9*4,11,9,3,2,90,80)<0 && errno==ENOTSUP && !gs->error);
 gs->caps.features=caps;
 assert(!ps2_pipe_cursor_set_argb(cursor,cursor_argb,11*9*4,11,9,3,2,90,80));argb_golden(32,11,9,3,2,90,80,1);finish();
}
static void argb_failure(void)
{
 cursor_setup(32);cursor=ps2_pipe_cursor_create(ctx);assert(cursor);argb_seed(11,9,0);argb_fail_ioctl=1;
 assert(ps2_pipe_cursor_set_argb(cursor,cursor_argb,11*9*4,11,9,3,2,90,80)<0 && errno==EIO);
 assert(ps2_pipe_context_render(ctx)->gs->error==EIO);argb_fail_ioctl=0;finish();
}
int main(int argc,char **argv)
{
 assert(argc==2);if(!strcmp(argv[1],"guards"))argb_guards();else if(!strcmp(argv[1],"failure"))argb_failure();else argb_case(argv[1]);
 puts("GS ARGB cursor case PASS");return 0;
}
