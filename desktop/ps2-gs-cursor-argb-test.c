/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "ps2_gs_render.h"
#include "ps2_cursor.h"
#include <pixman.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/kd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static uint32_t original[64*64],pixels[64*64],actual[2048],expected[2048];
static unsigned checks,images;
static uint32_t background;
static int read_block(struct ps2_surface_client *gs,unsigned x,unsigned y)
{
 unsigned bytes=64*32*(gs->front.bits_per_pixel/8);
 struct ps2gs_surface_download r={.abi=PS2GS_ABI_VERSION,.handle=0,.x=x,.y=y,.width=64,.height=32,.bytes=bytes,.pixels=(uintptr_t)actual};
 /* Observe the visible cursor directly. The public surface download removes
  * cursors for client replies; this diagnostic must inspect GS scanout pixels. */
 return ioctl(gs->fd,PS2GS_SURFACE_DOWNLOAD,&r);
}
static void seed(unsigned w,unsigned h,unsigned salt)
{
 static const unsigned alpha[]={0,1,64,128,254,255};
 for(unsigned i=0;i<w*h;++i){unsigned a=alpha[(i+salt)%6];
  pixels[i]=(a<<24)|((((i*19+salt*13)&255)*a+127)/255<<16)|
   ((((i*37+23)&255)*a+127)/255<<8)|(((i*59+91)&255)*a+127)/255;
 }memcpy(original,pixels,sizeof(pixels));
}
static int verify(struct ps2_surface_client *gs,unsigned w,unsigned h,unsigned hx,unsigned hy,int x,int y,int bx,int by,int visible)
{
 unsigned bpp=gs->front.bits_per_pixel,cpp=bpp/8,stride=64*cpp;
 bx=bx<0?0:bx;by=by<0?0:by;
 if(bx>(int)gs->front.width-64)bx=gs->front.width-64;
 if(by>(int)gs->front.height-32)by=gs->front.height-32;
 for(unsigned i=0;i<2048;++i)memcpy((unsigned char*)expected+i*cpp,&background,cpp);
 pixman_image_t *dst=pixman_image_create_bits(bpp==16?PIXMAN_x1b5g5r5:PIXMAN_x8b8g8r8,64,32,expected,stride);
 pixman_image_t *src=pixman_image_create_bits(PIXMAN_a8r8g8b8,w,h,original,w*4);
 if(!src || !dst){if(src)pixman_image_unref(src);if(dst)pixman_image_unref(dst);return -1;}
 int64_t left=(int64_t)x-hx-bx,top=(int64_t)y-hy-by;
 if(visible && left<64 && left+w>0 && top<32 && top+h>0)
  pixman_image_composite32(PIXMAN_OP_OVER,src,NULL,dst,0,0,0,0,left,top,w,h);
 pixman_image_unref(src);pixman_image_unref(dst);
 if(read_block(gs,bx,by))return -1;
 for(unsigned i=0;i<2048;++i){uint32_t a=0,e=0;memcpy(&a,(unsigned char*)actual+i*cpp,cpp);memcpy(&e,(unsigned char*)expected+i*cpp,cpp);
  uint32_t mask=visible?(bpp==16?0x7fff:0xffffff):bpp==16?0xffff:UINT32_MAX;
  if((a&mask)!=(e&mask)){fprintf(stderr,"ps2-cursor-argb: bpp=%u x=%u y=%u expected=%08x actual=%08x visible=%d\n",bpp,bx+i%64,by+i/64,e,a,visible);errno=EIO;return -1;}
  ++checks;
 }++images;return 0;
}
int main(void)
{
 int tty=-1,graphics=0,mode,status=1;struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;struct ps2_pipe_cursor *cursor=NULL;
 tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
 if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
 graphics=1;screen=ps2_pipe_screen_create("/dev/ps2-gs");if(!screen || !(context=screen->context_create(screen,NULL,0)))goto out;
 struct ps2_surface_client *gs=ps2_pipe_context_render(context)->gs;
 if(gs->front.width<64 || gs->front.height<32){errno=ENOTSUP;goto out;}
 cursor=ps2_pipe_cursor_create(context);if(!cursor || ps2_surface_fill(gs,0,0,0,gs->front.width,gs->front.height,0x803b71c9) || read_block(gs,0,0))goto out;
 memcpy(&background,actual,gs->front.bits_per_pixel/8);seed(11,9,0);
 int positions[][2]={{30,30},{0,0},{gs->front.width-1,gs->front.height-1},{-20,12},{INT_MAX,INT_MIN},{120,90}};
 int previous_x=0,previous_y=0;
 for(unsigned i=0;i<6;++i){int x=positions[i][0],y=positions[i][1];
  if(i?ps2_pipe_cursor_move(cursor,x,y):ps2_pipe_cursor_set_argb(cursor,pixels,11*9*4,11,9,3,2,x,y))goto out;
  if(!i)memset(pixels,0,sizeof(pixels));
  int bx=x<0 || x>=(int)gs->front.width?0:x-11,by=y<0 || y>=(int)gs->front.height?0:y-10;
  if(verify(gs,11,9,3,2,x,y,bx,by,1) || verify(gs,11,9,3,2,x,y,previous_x,previous_y,1))goto out;
  previous_x=bx;previous_y=by;
  printf("ps2-cursor-argb: position=%u passed\n",i);fflush(stdout);
 }
 if(ps2_surface_fill(gs,0,0,0,gs->front.width,gs->front.height,0x807b3d19) || read_block(gs,0,0))goto out;
 memcpy(&background,actual,gs->front.bits_per_pixel/8);
 if(ps2_pipe_cursor_flush(cursor) || verify(gs,11,9,3,2,120,90,109,80,1) || verify(gs,11,9,3,2,120,90,0,0,1))goto out;
 if(ps2_pipe_cursor_suspend(cursor,1) || verify(gs,11,9,3,2,120,90,109,80,0) || verify(gs,11,9,3,2,120,90,0,0,0))goto out;
 if(ps2_pipe_cursor_suspend(cursor,0) || verify(gs,11,9,3,2,120,90,109,80,1) || verify(gs,11,9,3,2,120,90,0,0,1))goto out;
 seed(7,5,3);
 if(ps2_pipe_cursor_set_argb(cursor,pixels,7*5*4,7,5,6,4,120,90) || verify(gs,7,5,6,4,120,90,109,80,1) || verify(gs,7,5,6,4,120,90,0,0,1))goto out;
 if(ps2_pipe_cursor_set(cursor,NULL,0,0) || verify(gs,7,5,6,4,120,90,109,80,0) || verify(gs,7,5,6,4,120,90,0,0,0))goto out;
 if(checks!=45056 || images!=22){errno=EPROTO;goto out;}status=0;
out:
 if(status)perror("ps2-cursor-argb: native diagnostic");
 if(cursor && ps2_pipe_cursor_destroy(cursor))status=1;
 if(context)context->destroy(context);
 if(screen)screen->destroy(screen);
 if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
 if(tty>=0)close(tty);
 printf("ps2-cursor-argb: pixel_checks=%u image_checks=%u exit=%d\n",checks,images,status);return status;
}
