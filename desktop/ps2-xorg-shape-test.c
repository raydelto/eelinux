/* SPDX-License-Identifier: MIT */
/* Client oracle: compare Shape/GC clip geometry with GS-produced A1 masks. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>

static Display *display;
static unsigned checks,errors;
static int xerror(Display *d,XErrorEvent *event)
{
    char text[128];XGetErrorText(d,event->error_code,text,sizeof(text));
    fprintf(stderr,"ps2-shape: X error %s request=%u minor=%u\n",text,event->request_code,event->minor_code);
    ++errors;return 0;
}
static void shape_check(Window window,XImage *image,int kind,int ox,int oy)
{
    int count,ordering;
    XRectangle *rects=XShapeGetRectangles(display,window,kind,&count,&ordering);
    unsigned width=image->width+12,height=image->height+12;
    unsigned char *actual=calloc(width,height);if(!actual)exit(2);
    for(int i=0;i<count;++i){
        int l=rects[i].x+6,t=rects[i].y+6;
        for(unsigned y=0;y<rects[i].height;++y)for(unsigned x=0;x<rects[i].width;++x){
            if(l+(int)x<0 || t+(int)y<0 || l+x>=width || t+y>=height){
                fprintf(stderr,"ps2-shape: rectangle outside mask bounds\n");exit(1);}
            actual[(t+y)*width+l+x]=1;
        }
    }
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        int sx=(int)x-6-ox,sy=(int)y-6-oy;
        unsigned expected=sx>=0 && sy>=0 && sx<image->width && sy<image->height && XGetPixel(image,sx,sy);
        if(actual[y*width+x]!=expected){
            fprintf(stderr,"ps2-shape: region mismatch kind=%d x=%u y=%u\n",kind,x,y);exit(1);}
    }
    XFree(rects);free(actual);++checks;
}
int main(void)
{
    display=XOpenDisplay(NULL);if(!display)return 2;
    XSetErrorHandler(xerror);
    int screen=DefaultScreen(display),event,error;
    if(!XShapeQueryExtension(display,&event,&error))return 2;
    Window root=RootWindow(display,screen);
    unsigned long foreground=WhitePixel(display,screen);
    const unsigned widths[]={1,7,8,31,32,33,65,150};
    for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i)for(unsigned pattern=0;pattern<4;++pattern){
        unsigned w=widths[i],h=w==150?100:9;
        printf("ps2-shape: width=%u height=%u pattern=%u\n",w,h,pattern);fflush(stdout);
        Pixmap mask=XCreatePixmap(display,root,w,h,1);
        GC mgc=XCreateGC(display,mask,0,NULL);
        XSetForeground(display,mgc,0);XFillRectangle(display,mask,mgc,0,0,w,h);
        XSetForeground(display,mgc,1);
        if(pattern==1)XFillRectangle(display,mask,mgc,0,0,w,h);
        if(pattern==2){
            XRectangle bars[64];unsigned n=0;
            for(unsigned x=0;x<w;x+=4)bars[n++]=(XRectangle){x,0,1,h};
            XFillRectangles(display,mask,mgc,bars,n);
            XSetForeground(display,mgc,0);XFillRectangle(display,mask,mgc,0,h/2,w,1);
        }
        if(pattern==3){
            unsigned eye=w>1?w/2:1;
            XArc arcs[2]={{0,0,eye,h,0,360*64},{w-eye,0,eye,h,0,360*64}};
            XFillArcs(display,mask,mgc,arcs,2);
        }
        XImage *image=XGetImage(display,mask,0,0,w,h,1,ZPixmap);if(!image)return 2;
        Window window=XCreateSimpleWindow(display,root,0,0,w,h,0,0,0);
        const int kinds[]={ShapeBounding,ShapeClip,ShapeInput};
        const int offsets[3][2]={{0,0},{-3,2},{5,-4}};
        for(unsigned j=0;j<3;++j){
            XShapeCombineMask(display,window,kinds[j],offsets[j][0],offsets[j][1],mask,ShapeSet);
            shape_check(window,image,kinds[j],offsets[j][0],offsets[j][1]);
        }
        Pixmap target=XCreatePixmap(display,root,w,h,DefaultDepth(display,screen));
        GC gc=XCreateGC(display,target,0,NULL);
        XSetForeground(display,gc,0);XFillRectangle(display,target,gc,0,0,w,h);
        XSetClipMask(display,gc,mask);XSetClipOrigin(display,gc,-3,2);
        XSetForeground(display,gc,foreground);XFillRectangle(display,target,gc,0,0,w,h);
        XImage *reply=XGetImage(display,target,0,0,w,h,AllPlanes,ZPixmap);if(!reply)return 2;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            int sx=x+3,sy=(int)y-2;
            unsigned long expected=sx<(int)w && sy>=0 && XGetPixel(image,sx,sy)?foreground:0;
            if(XGetPixel(reply,x,y)!=expected){
                fprintf(stderr,"ps2-shape: GC clip mismatch x=%u y=%u\n",x,y);return 1;}
        }
        ++checks;
        XDestroyImage(reply);XDestroyImage(image);XFreeGC(display,gc);XFreePixmap(display,target);
        XDestroyWindow(display,window);XFreeGC(display,mgc);XFreePixmap(display,mask);
    }
    XSync(display,False);XCloseDisplay(display);
    printf("ps2-shape: completed checks=%u errors=%u\n",checks,errors);
    return errors || checks!=128?1:0;
}
