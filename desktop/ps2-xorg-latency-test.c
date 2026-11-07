/* SPDX-License-Identifier: MIT */
/* Diagnostic only: round-trip native pointer motion while other clients draw. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static uint64_t clock_ns(void)
{
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))exit(2);
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static int compare(const void *a,const void *b)
{uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    Display *d=XOpenDisplay(NULL);if(!d)return 2;
    Window root=DefaultRootWindow(d);
    Cursor cursor=XCreateFontCursor(d,XC_left_ptr);XDefineCursor(d,root,cursor);
    uint64_t samples[8192],total=0;unsigned count=0;
    do{
        unsigned x=200+count%180,y=180+count%120;
        uint64_t start=clock_ns();
        XWarpPointer(d,None,root,0,0,0,0,x,y);
        Window actual_root,child;int rx,ry,wx,wy;unsigned mask;
        if(!XQueryPointer(d,root,&actual_root,&child,&rx,&ry,&wx,&wy,&mask) || rx!=(int)x || ry!=(int)y)return 1;
        uint64_t elapsed=clock_ns()-start;total+=elapsed;
        if(count<8192)samples[count]=elapsed;
        ++count;
        struct timespec delay={.tv_nsec=50000000};nanosleep(&delay,NULL);
    }while(access(argv[1],F_OK));
    unsigned stored=count<8192?count:8192;
    qsort(samples,stored,sizeof(*samples),compare);
    printf("ps2-latency: samples=%u mean_ns=%llu p50_ns=%llu p95_ns=%llu max_ns=%llu\n",count,
        (unsigned long long)(total/count),(unsigned long long)samples[stored/2],
        (unsigned long long)samples[(stored-1)*95/100],(unsigned long long)samples[stored-1]);
    XUndefineCursor(d,root);XFreeCursor(d,cursor);XCloseDisplay(d);return 0;
}
