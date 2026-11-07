/* SPDX-License-Identifier: MIT */
/* A real Xlib client for the native DDX; every colour pixel is drawn by GS. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
static int errors;
static void scene(Display *,Window,GC,Pixmap,int,unsigned long *);
static int xerror(Display *d,XErrorEvent *e)
{char text[128];XGetErrorText(d,e->error_code,text,sizeof(text));fprintf(stderr,"ps2-xorg: protocol error %s request=%u minor=%u\n",text,e->request_code,e->minor_code);++errors;return 0;}
static unsigned long colour(Display *d,const char *name)
{XColor a,b;if(!XAllocNamedColor(d,DefaultColormap(d,DefaultScreen(d)),name,&a,&b)){fprintf(stderr,"ps2-xorg: colour unavailable\n");exit(1);}return a.pixel;}
static void settle(Display *d,unsigned ticks)
{
    while(ticks--){XSync(d,False);while(XPending(d)){XEvent e;XNextEvent(d,&e);}
        struct timespec pause={0,100000000};nanosleep(&pause,NULL);}
}
static void slave_motion_check(Display *d,Window root)
{
    int count=0;XDeviceInfo *devices=XListInputDevices(d,&count);XDevice *mouse=NULL;
    for(int i=0;devices && i<count;++i){
        if(devices[i].name && !strcmp(devices[i].name,"Mouse0"))mouse=XOpenDevice(d,devices[i].id);
    }
    if(devices)XFreeDeviceList(devices);
    if(!mouse){++errors;printf("ps2-xorg: mouse slave input unavailable\n");return;}
    Window rr,child;int rx=0,ry=0,wx,wy;unsigned mask;
    Bool before=XQueryPointer(d,root,&rr,&child,&rx,&ry,&wx,&wy,&mask);
    int previous_x=rx,previous_y=ry,axes[]={24,16};
    if(!XTestFakeDeviceMotionEvent(d,mouse,True,0,axes,2,CurrentTime))++errors;
    settle(d,10);
    if(!before || !XQueryPointer(d,root,&rr,&child,&rx,&ry,&wx,&wy,&mask) ||
       (rx==previous_x && ry==previous_y))++errors;
    XCloseDevice(d,mouse);XSync(d,False);
    printf("ps2-xorg: attached mouse slave motion errors=%d\n",errors);
}
static Window named_window(Display *d,Window parent,const char *title,unsigned depth)
{
    char *name=NULL;Window match=None;
    if(XFetchName(d,parent,&name) && name && !strcmp(name,title))match=parent;
    if(name)XFree(name);
    if(match || !depth)return match;
    Window root,owner,*children=NULL;unsigned count=0;
    if(XQueryTree(d,parent,&root,&owner,&children,&count)){
        for(unsigned i=0;i<count && !match;++i)match=named_window(d,children[i],title,depth-1);
    }
    if(children)XFree(children);
    return match;
}
struct keyboard_map { int first,last,width;KeySym *symbols;KeyCode shift; };
static void key(Display *d,const struct keyboard_map *map,KeySym symbol)
{
    KeyCode code=XKeysymToKeycode(d,symbol);
    if(!map->symbols || !code || code<map->first || code>map->last){++errors;return;}
    KeySym *mapping=map->symbols+(code-map->first)*map->width;
    Bool shift=map->width>1 && mapping[0]!=symbol && mapping[1]==symbol;
    if((!shift && mapping[0]!=symbol) || (shift && !map->shift)){++errors;return;}
    if(shift && !XTestFakeKeyEvent(d,map->shift,True,CurrentTime))++errors;
    if(!XTestFakeKeyEvent(d,code,True,CurrentTime) || !XTestFakeKeyEvent(d,code,False,CurrentTime))++errors;
    if(shift && !XTestFakeKeyEvent(d,map->shift,False,CurrentTime))++errors;
}
static void typed(Display *d,const struct keyboard_map *map,const char *text)
{while(*text)key(d,map,*text=='\n'?XK_Return:(unsigned char)*text),++text;}
static void interactive_terminal_check(Display *d,Window root)
{
    const char *title="PS2 interactive terminal input test";
    const char *marker="/var/lib/ps2-xorg-test/keyboard-input.ok";
    unlink(marker);
    pid_t terminal=fork();
    if(terminal==0){close(ConnectionNumber(d));execlp("xterm","xterm","-name","ps2-terminal",
        "-title",title,"-geometry","50x10+30+320","-fn","6x13","-fb","6x13bold",
        "-xrm","XTerm*renderFont:false","-e","/bin/bash","--noprofile","--norc","-i",(char*)NULL);_exit(127);}
    Window window=None;int status=0;Bool alive=terminal>0;
    for(unsigned attempt=0;alive && !window && attempt<80;++attempt){
        settle(d,5);if(waitpid(terminal,&status,WNOHANG)==terminal)alive=False;
        else {
            window=named_window(d,root,title,4);
            XWindowAttributes a;
            if(window && (!XGetWindowAttributes(d,window,&a) || a.map_state!=IsViewable))window=None;
        }
    }
    if(!window || !alive)++errors;
    else {
        /* TWM assigns focus when the pointer enters a managed window. Let
         * that transition finish before injecting keys, or its pending
         * EnterNotify handling can undo a focus set on a newly mapped xterm. */
        XRaiseWindow(d,window);XWarpPointer(d,None,window,0,0,0,0,20,20);settle(d,10);
        XSetInputFocus(d,window,RevertToPointerRoot,CurrentTime);XSync(d,False);
        Window focused=None;int revert=0;XGetInputFocus(d,&focused,&revert);
        if(focused!=window)++errors;
        printf("ps2-xorg: interactive terminal focus target=%lx actual=%lx errors=%d\n",window,focused,errors);
        struct keyboard_map map={0};XDisplayKeycodes(d,&map.first,&map.last);
        map.symbols=XGetKeyboardMapping(d,map.first,map.last-map.first+1,&map.width);
        map.shift=XKeysymToKeycode(d,XK_Shift_L);
        typed(d,&map,"echo ps2-input-ox");key(d,&map,XK_BackSpace);
        typed(d,&map,"k > /var/lib/ps2-xorg-test/keyboard-input.ok\n");
        if(map.symbols)XFree(map.symbols);
        XSync(d,False);
        XGetInputFocus(d,&focused,&revert);
        if(focused!=window)++errors;
        printf("ps2-xorg: interactive terminal focus after typing=%lx errors=%d\n",focused,errors);
        Bool executed=False;
        for(unsigned attempt=0;attempt<80 && !executed;++attempt){
            settle(d,5);FILE *file=fopen(marker,"r");
            if(file){char line[64];executed=fgets(line,sizeof(line),file) && !strcmp(line,"ps2-input-ok\n");fclose(file);}
            pid_t ended=waitpid(terminal,&status,WNOHANG);
            if(ended==terminal){alive=False;break;}
        }
        if(!executed || !alive)++errors;
        /* A successful typed command must leave the interactive shell alive. */
        if(alive){settle(d,20);if(waitpid(terminal,&status,WNOHANG)==terminal){alive=False;++errors;}}
    }
    printf("ps2-xorg: interactive terminal typing/backspace errors=%d alive=%d status=%d\n",errors,alive,status);
    if(alive){kill(terminal,SIGTERM);while(waitpid(terminal,&status,0)<0 && errno==EINTR){}}
    XSync(d,False);
}
static void desktop_checks(Display *d,Window root,Window w,GC g,Pixmap tile,unsigned long *c)
{
    const char bits[32]={0x55,0x55,(char)0xaa,(char)0xaa,0x55,0x55,(char)0xaa,(char)0xaa,
        0x55,0x55,(char)0xaa,(char)0xaa,0x55,0x55,(char)0xaa,(char)0xaa,
        0x55,0x55,(char)0xaa,(char)0xaa,0x55,0x55,(char)0xaa,(char)0xaa,
        0x55,0x55,(char)0xaa,(char)0xaa,0x55,0x55,(char)0xaa,(char)0xaa};
    Pixmap mono=XCreateBitmapFromData(d,w,bits,16,16);
    XSetForeground(d,g,c[1]);XSetBackground(d,g,c[0]);XSetFunction(d,g,GXcopy);
    XCopyPlane(d,mono,w,g,0,0,16,16,25,235,1);XFreePixmap(d,mono);XSync(d,False);
    printf("ps2-xorg: monochrome CopyPlane synchronized errors=%d\n",errors);
    int s=DefaultScreen(d),width=DisplayWidth(d,s),height=DisplayHeight(d,s);
    int positions[][2]={{0,0},{width-1,height-1},{width/2,height/2},{40,40}};
    for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);++i){
        XWarpPointer(d,None,root,0,0,0,0,positions[i][0],positions[i][1]);XSync(d,False);
        Window rr,child;int rx,ry,wx,wy;unsigned mask;
        if(!XQueryPointer(d,root,&rr,&child,&rx,&ry,&wx,&wy,&mask) || rx!=positions[i][0] || ry!=positions[i][1])++errors;
    }
    printf("ps2-xorg: cursor warp and edge checks errors=%d\n",errors);
    slave_motion_check(d,root);
    XMoveResizeWindow(d,w,125,95,460,290);settle(d,10);
    XWindowAttributes a;if(!XGetWindowAttributes(d,w,&a) || a.width!=460 || a.height!=290)++errors;
    /* Resize exposes new storage; repaint the demo after draining WM events. */
    if(!errors)scene(d,w,g,tile,3,c);
    printf("ps2-xorg: managed window move/resize errors=%d\n",errors);
    pid_t terminal=fork();
    if(terminal==0){close(ConnectionNumber(d));execlp("xterm","xterm","-title","PS2 native terminal test",
        "-geometry","50x10+30+320","-fn","6x13","-xrm","XTerm*renderFont:false","-e","/bin/bash","-c",
        "printf 'Native Xorg terminal / core fonts / scrolling\\n'; for ((i=1;i<=24;i++)); do printf 'PS2 terminal line %d\\n' \"$i\"; done; sleep 5",(char*)NULL);_exit(127);}
    int status=0;
    if(terminal<0 || waitpid(terminal,&status,0)!=terminal || !WIFEXITED(status) || WEXITSTATUS(status))++errors;
    XSync(d,False);printf("ps2-xorg: xterm core-font scrolling completed errors=%d\n",errors);
    if(!errors)interactive_terminal_check(d,root);
    /* Xterm has exited, but TWM can still be processing its frame removal.
     * Let the resulting Expose/clear requests finish before the final redraw. */
    if(!errors){settle(d,10);scene(d,w,g,tile,3,c);settle(d,5);}
    FILE *memory=fopen("/proc/meminfo","r");
    if(memory){
        char line[160];
        while(fgets(line,sizeof(line),memory)){
            if(!strncmp(line,"MemTotal:",9) || !strncmp(line,"MemAvailable:",13))printf("ps2-xorg: %s",line);
        }
        fclose(memory);
    }
}
static void scene_pixels(Display *d,Window w,int n,unsigned long *c)
{
    XImage *image=XGetImage(d,w,0,0,440,270,AllPlanes,ZPixmap);
    if(!image){++errors;return;}
    int points[][2]={{10,250},{30,70},{55,205},{260,205}};
    for(unsigned i=0;i<(n==3?4U:1U);++i){unsigned long wanted=i?c[i==1?4:2]:c[0];
        unsigned long actual=XGetPixel(image,points[i][0],points[i][1]);
        if(actual!=wanted){++errors;printf("ps2-xorg: scene=%d pixel mismatch x=%d y=%d expected=%lx actual=%lx\n",n+1,points[i][0],points[i][1],wanted,actual);}}
    XDestroyImage(image);
    /* Read-only serialization of GS scanout supplies visual evidence too. */
    int s=DefaultScreen(d);image=XGetImage(d,RootWindow(d,s),0,0,DisplayWidth(d,s),DisplayHeight(d,s),AllPlanes,ZPixmap);
    if(!image){++errors;return;}
    char name[128];snprintf(name,sizeof(name),"/var/lib/ps2-xorg-test/scene-%d.ppm",n+1);
    FILE *file=fopen(name,"wb");if(!file){XDestroyImage(image);++errors;return;}
    fprintf(file,"P6\n%d %d\n255\n",image->width,image->height);
    unsigned masks[3]={image->red_mask,image->green_mask,image->blue_mask};
    for(int y=0;y<image->height;++y)for(int x=0;x<image->width;++x){unsigned long p=XGetPixel(image,x,y);
        for(unsigned k=0;k<3;++k){unsigned mask=masks[k],shift=0;while(mask && !(mask&1)){mask>>=1;++shift;}
            fputc(mask?((p>>shift)&mask)*255/mask:0,file);}}
    fflush(file);fsync(fileno(file));fclose(file);XDestroyImage(image);printf("ps2-xorg: scene=%d native pixel/scanout capture errors=%d\n",n+1,errors);
}
static void scene(Display *d,Window w,GC g,Pixmap tile,int n,unsigned long *c)
{
    struct timespec started,finished;
    XSync(d,False);clock_gettime(CLOCK_MONOTONIC,&started);
    XSetFunction(d,g,GXcopy);XSetPlaneMask(d,g,AllPlanes);XSetFillStyle(d,g,FillSolid);
    XSetForeground(d,g,c[0]);XFillRectangle(d,w,g,0,0,440,270);
    XSetForeground(d,g,c[1]);XSetLineAttributes(d,g,0,LineSolid,CapButt,JoinMiter);
    const char label[]="Native GS / Xorg / Mesa integration";
    XDrawString(d,w,g,12,20,label,sizeof(label)-1);
    if(n==0){
        XSetForeground(d,g,c[2]);XSetLineAttributes(d,g,9,LineSolid,CapRound,JoinRound);
        XPoint p[]={{30,90},{120,40},{210,140},{300,55},{400,110}};XDrawLines(d,w,g,p,5,CoordModeOrigin);
        XSetForeground(d,g,c[3]);XDrawArc(d,w,g,35,155,150,80,0,300*64);
        XSetForeground(d,g,c[4]);XFillArc(d,w,g,240,155,150,80,20*64,280*64);
    }else if(n==1){
        XSetForeground(d,g,c[2]);XSetBackground(d,g,c[3]);char dash[]={9,5,3,5};XSetDashes(d,g,7,dash,4);
        XSetLineAttributes(d,g,7,LineDoubleDash,CapProjecting,JoinBevel);
        XPoint p[]={{25,75},{110,160},{210,65},{320,160},{410,75}};XDrawLines(d,w,g,p,5,CoordModeOrigin);
        XDrawArc(d,w,g,130,160,170,80,0,360*64);
    }else if(n==2){
        XSetTile(d,g,tile);XSetTSOrigin(d,g,-3,5);XSetFillStyle(d,g,FillTiled);
        XPoint p[]={{30,95},{150,95},{150,50},{240,120},{150,190},{150,145},{30,145}};
        XFillPolygon(d,w,g,p,7,Nonconvex,CoordModeOrigin);
        XFillArc(d,w,g,270,70,130,130,0,360*64);
    }else{
        XSetForeground(d,g,c[4]);XFillRectangle(d,w,g,25,65,170,130);
        XSetForeground(d,g,c[2]);XFillRectangle(d,w,g,45,85,170,130);
        XCopyArea(d,w,w,g,25,65,190,150,230,65);
        XSetFunction(d,g,GXxor);XSetForeground(d,g,c[1]);XSetLineAttributes(d,g,11,LineSolid,CapRound,JoinRound);
        XDrawArc(d,w,g,90,90,250,110,0,360*64);XSetFunction(d,g,GXcopy);
    }
    XSync(d,False);clock_gettime(CLOCK_MONOTONIC,&finished);
    double elapsed=(finished.tv_sec-started.tv_sec)*1000.0+(finished.tv_nsec-started.tv_nsec)/1000000.0;
    printf("ps2-xorg: scene=%d GS drawing milliseconds=%.3f\n",n+1,elapsed);
    scene_pixels(d,w,n,c);printf("ps2-xorg: scene=%d synchronized errors=%d\n",n+1,errors);fflush(stdout);
}
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);Display *d=XOpenDisplay(NULL);if(!d){fprintf(stderr,"ps2-xorg: cannot connect\n");return 1;}
    XSetErrorHandler(xerror);int s=DefaultScreen(d);Window root=RootWindow(d,s);
    printf("ps2-xorg: connected vendor=%s depth=%d size=%dx%d\n",ServerVendor(d),DefaultDepth(d,s),DisplayWidth(d,s),DisplayHeight(d,s));
    pid_t wm=fork();if(wm==0){close(ConnectionNumber(d));execlp("twm","twm",(char*)NULL);_exit(127);}
    unsigned long c[]={colour(d,"#080d20"),colour(d,"white"),colour(d,"cyan"),colour(d,"yellow"),colour(d,"magenta")};
    printf("ps2-xorg: colours background=%lx white=%lx cyan=%lx yellow=%lx magenta=%lx\n",c[0],c[1],c[2],c[3],c[4]);
    Window w=XCreateSimpleWindow(d,root,90,80,440,270,2,c[1],c[0]);XStoreName(d,w,"PS2 native 2D integration");
    XSizeHints hints={.flags=USPosition|USSize,.x=90,.y=80,.width=440,.height=270};XSetWMNormalHints(d,w,&hints);
    XSelectInput(d,w,ExposureMask|StructureNotifyMask);Cursor cursor=XCreateFontCursor(d,XC_left_ptr);XDefineCursor(d,w,cursor);
    GC g=XCreateGC(d,w,0,NULL);Pixmap tile=XCreatePixmap(d,w,16,16,DefaultDepth(d,s));
    XSetForeground(d,g,c[2]);XFillRectangle(d,tile,g,0,0,16,16);XSetForeground(d,g,c[3]);XFillRectangle(d,tile,g,0,0,8,8);XFillRectangle(d,tile,g,8,8,8,8);
    XMapWindow(d,w);settle(d,10);
    const char *duration=getenv("PS2_TEST_SECONDS");int seconds=duration?atoi(duration):48;if(seconds<8 || seconds>600)seconds=48;
    for(int n=0;n<4 && !errors;++n){scene(d,w,g,tile,n,c);settle(d,2);}
    time_t begin=time(NULL);int current=-1;while(seconds>8 && time(NULL)-begin<seconds && !errors){
        int next=(time(NULL)-begin)*4/seconds;if(next>3)next=3;Bool redraw=next!=current;
        while(XPending(d)){XEvent e;XNextEvent(d,&e);if(e.type==Expose)redraw=True;}
        if(redraw){scene(d,w,g,tile,next,c);current=next;}
        struct timespec pause={0,100000000};nanosleep(&pause,NULL);
    }
    if(!errors)desktop_checks(d,root,w,g,tile,c);
    XFreePixmap(d,tile);XFreeGC(d,g);XFreeCursor(d,cursor);XDestroyWindow(d,w);XSync(d,False);
    if(wm>0){kill(wm,SIGTERM);waitpid(wm,NULL,0);}XCloseDisplay(d);
    printf("ps2-xorg: client completed errors=%d\n",errors);return errors?1:0;
}
