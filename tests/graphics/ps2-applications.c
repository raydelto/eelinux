/* SPDX-License-Identifier: MIT. Qualification client, never shipped on media. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
static int errors;
static int error(Display *d,XErrorEvent *e){char s[160];XGetErrorText(d,e->error_code,s,sizeof(s));
 fprintf(stderr,"desktop: protocol error %s request=%u\n",s,e->request_code);++errors;return 0;}
static void pause_tick(void){struct timespec t={0,500000000};nanosleep(&t,NULL);}
static Window find(Display *d,Window w,const char *name,unsigned depth)
{
 XClassHint hint={0};Window found=None;
 if(XGetClassHint(d,w,&hint)){
  if(hint.res_name && !strcmp(hint.res_name,name)){XWindowAttributes a;
   if(XGetWindowAttributes(d,w,&a) && a.map_state==IsViewable)found=w;}
  if(hint.res_name)XFree(hint.res_name);
  if(hint.res_class)XFree(hint.res_class);
 }
 if(found || !depth)return found;
 Window root,parent,*children=NULL;unsigned count=0;
 if(XQueryTree(d,w,&root,&parent,&children,&count))
  for(unsigned i=0;i<count && !found;++i)found=find(d,children[i],name,depth-1);
 if(children)XFree(children);
 return found;
}
static int await_window(Display *d,const char *name)
{
 for(unsigned i=0;i<90;++i){XSync(d,False);if(find(d,DefaultRootWindow(d),name,5)){
  printf("desktop: mapped %s errors=%d\n",name,errors);fflush(stdout);return 1;}pause_tick();}
 fprintf(stderr,"desktop: missing %s\n",name);++errors;return 0;
}
static pid_t launch(Display *d,const char *app,const char *name,const char *geometry)
{
 pid_t pid=fork();if(!pid){close(ConnectionNumber(d));execlp(app,app,"-name",name,"-geometry",geometry,(char*)NULL);_exit(127);}return pid;
}
static int await_manager(Display *d,const char *name,const char *label)
{
 for(unsigned i=0;i<90;++i){
  Window w=find(d,DefaultRootWindow(d),name,5),root,parent,*children=NULL;unsigned count=0;
  if(w && XQueryTree(d,w,&root,&parent,&children,&count)){
   if(children)XFree(children);
   if(parent!=root){printf("desktop: TWM manages %s errors=%d\n",label,errors);fflush(stdout);return 1;}
  }
  pause_tick();
 }
 fprintf(stderr,"desktop: %s has no window manager\n",label);++errors;return 0;
}
static void cursor_pixels(Display *d)
{
 /* An independent X client checks the native driver's save-under after
  * repeated movement across high contrast pixels, including odd x/y. */
 XSetWindowAttributes a={.override_redirect=True,.background_pixel=BlackPixel(d,DefaultScreen(d))};
 Window w=XCreateWindow(d,DefaultRootWindow(d),37,83,384,180,0,CopyFromParent,InputOutput,CopyFromParent,
    CWOverrideRedirect|CWBackPixel,&a);XMapRaised(d,w);
 GC gc=XCreateGC(d,w,0,NULL);XSetForeground(d,gc,WhitePixel(d,DefaultScreen(d)));
 for(int y=0;y<180;y+=9)for(int x=0;x<384;x+=7)XFillRectangle(d,w,gc,x,y,3,5);
 char bits[128],mask[128];memset(mask,255,sizeof(mask));
 for(unsigned i=0;i<sizeof(bits);++i)bits[i]=(char)(i*37+19);
 Pixmap source=XCreateBitmapFromData(d,w,bits,32,32),cover=XCreateBitmapFromData(d,w,mask,32,32);
 XColor fg={.red=65535,.green=65535,.blue=65535},bg={0};
 Cursor cursor=XCreatePixmapCursor(d,source,cover,&fg,&bg,7,9);XDefineCursor(d,w,cursor);XSync(d,False);
 unsigned mismatches=0;
 for(unsigned batch=0;batch<8;++batch){
  for(unsigned i=0;i<24;++i){unsigned n=batch*24+i;
   XWarpPointer(d,None,w,0,0,0,0,(n*37)%384,(n*53)%180);XSync(d,False);
  }
  XImage *im=XGetImage(d,w,0,0,384,180,AllPlanes,ZPixmap);
  if(!im){++errors;break;}
  for(int y=0;y<180;++y)for(int x=0;x<384;++x){
   unsigned long expected=(x%7<3 && y%9<5)?WhitePixel(d,DefaultScreen(d)):BlackPixel(d,DefaultScreen(d));
   mismatches+=XGetPixel(im,x,y)!=expected;
  }
  XDestroyImage(im);
 }
 if(mismatches)++errors;
 XWarpPointer(d,None,DefaultRootWindow(d),0,0,0,0,10,10);XSync(d,False);
 XFreeCursor(d,cursor);XFreePixmap(d,source);XFreePixmap(d,cover);XFreeGC(d,gc);XDestroyWindow(d,w);XSync(d,False);
 printf("desktop: cursor 192 moves mismatched_pixels=%u errors=%d\n",mismatches,errors);fflush(stdout);
}
static void capture(Display *d)
{
 int s=DefaultScreen(d);XImage *im=XGetImage(d,RootWindow(d,s),0,0,DisplayWidth(d,s),DisplayHeight(d,s),AllPlanes,ZPixmap);
 if(!im){++errors;return;}
 FILE *f=fopen("/var/lib/desktop-qualification/desktop.ppm","wb");if(!f){++errors;XDestroyImage(im);return;}
 fprintf(f,"P6\n%d %d\n255\n",im->width,im->height);unsigned masks[]={im->red_mask,im->green_mask,im->blue_mask};
 for(int y=0;y<im->height;++y)for(int x=0;x<im->width;++x){unsigned long p=XGetPixel(im,x,y);
  for(unsigned k=0;k<3;++k){unsigned mask=masks[k],shift=0;while(mask && !(mask&1)){mask>>=1;++shift;}
   fputc(mask?((p>>shift)&mask)*255/mask:0,f);}}
 fclose(f);XDestroyImage(im);
}
static void clock_pixels(Display *d)
{
 Window w=find(d,DefaultRootWindow(d),"ps2-clock",5);XWindowAttributes a;
 if(!w || !XGetWindowAttributes(d,w,&a) || a.width<90 || a.height<90){
  fprintf(stderr,"desktop: analogue clock geometry missing\n");++errors;return;
 }
 XImage *im=XGetImage(d,w,0,0,a.width,a.height,AllPlanes,ZPixmap);
 if(!im){++errors;return;}
 unsigned black=0,white=0;
 for(int y=0;y<im->height;++y)for(int x=0;x<im->width;++x){
  unsigned long p=XGetPixel(im,x,y);
  black+=p==BlackPixel(d,DefaultScreen(d));white+=p==WhitePixel(d,DefaultScreen(d));
 }
 XDestroyImage(im);
 if(black<6 || white<6)++errors;
 printf("desktop: analogue clock %dx%d black=%u white=%u errors=%d\n",a.width,a.height,black,white,errors);fflush(stdout);
}
static void benchmark(Display *d)
{
 XSetWindowAttributes a={.override_redirect=True,.background_pixel=BlackPixel(d,DefaultScreen(d))};
 Window w=XCreateWindow(d,DefaultRootWindow(d),220,250,400,220,0,CopyFromParent,InputOutput,CopyFromParent,
    CWOverrideRedirect|CWBackPixel,&a);XMapRaised(d,w);XSync(d,False);
 GC gc=XCreateGC(d,w,0,NULL);XSetForeground(d,gc,WhitePixel(d,DefaultScreen(d)));
 XRectangle rectangles[64];for(unsigned i=0;i<64;++i)rectangles[i]=(XRectangle){i%16*24,i/16*40,20,30};
 XPoint polygon[]={{20,20},{150,20},{150,0},{220,80},{150,150},{150,120},{20,120}};
 for(unsigned kind=0;kind<3;++kind){
  struct timespec start,end;XSync(d,False);clock_gettime(CLOCK_MONOTONIC,&start);
  for(unsigned i=0;i<4;++i){
   if(kind==0)XFillRectangles(d,w,gc,rectangles,64);
   else if(kind==1)XFillArc(d,w,gc,10,10,370,180,0,360*64);
   else XFillPolygon(d,w,gc,polygon,7,Complex,CoordModeOrigin);
  }
  XSync(d,False);clock_gettime(CLOCK_MONOTONIC,&end);
  printf("desktop: benchmark %s milliseconds=%.3f errors=%d\n",kind==0?"rectangles":kind==1?"arcs":"polygons",
   (end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0,errors);fflush(stdout);
 }
 XFreeGC(d,gc);XDestroyWindow(d,w);XSync(d,False);
}
int main(void)
{
 Display *d=NULL;for(unsigned i=0;i<160 && !d;++i){d=XOpenDisplay(NULL);if(!d)pause_tick();}
 if(!d)return 1;
 XSetErrorHandler(error);
 printf("desktop: X server connected\n");fflush(stdout);
 if(!await_window(d,"ps2-clock") || !await_window(d,"ps2-terminal"))return 1;
 if(!await_manager(d,"ps2-terminal","terminal") || !await_manager(d,"ps2-clock","clock"))return 1;
 pid_t calc=launch(d,"xcalc","ps2-calc","+20+70");await_window(d,"ps2-calc");
 pid_t eyes=launch(d,"xeyes","ps2-eyes","120x80-15+150");await_window(d,"ps2-eyes");
 for(unsigned i=0;i<5;++i)pause_tick();
 clock_pixels(d);
 cursor_pixels(d);
 XWarpPointer(d,None,DefaultRootWindow(d),0,0,0,0,10,10);XSync(d,False);
 capture(d);
 benchmark(d);
 if(calc>0){kill(calc,SIGTERM);waitpid(calc,NULL,0);}if(eyes>0){kill(eyes,SIGTERM);waitpid(eyes,NULL,0);}
 XSync(d,False);XCloseDisplay(d);printf("desktop: completed errors=%d\n",errors);return errors?1:0;
}
