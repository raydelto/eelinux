/* SPDX-License-Identifier: MIT. Qualification client; never installed on production media. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/shape.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <limits.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static Display *display;
static unsigned protocol_errors;
static const char *qualifier;
static volatile sig_atomic_t child_group;
static void interrupted(int signal_number)
{
    if(child_group>0)kill(-child_group,SIGKILL);
    _exit(128+signal_number);
}
static int xerror(Display *d,XErrorEvent *event)
{
    char text[160];XGetErrorText(d,event->error_code,text,sizeof(text));
    fprintf(stderr,"application: X error=%s request=%u resource=%lu\n",text,event->request_code,event->resourceid);
    ++protocol_errors;return 0;
}
static void pause_tick(void){struct timespec t={0,200000000};nanosleep(&t,NULL);}
static const char *filename(const char *p){const char *last=strrchr(p,'/');return last?last+1:p;}
static Window find_window(Window window,const char *command,unsigned depth)
{
    if(!depth)return None;
    char **arguments=NULL;int count=0;Window found=None;
    if(XGetCommand(display,window,&arguments,&count) && count){
        XWindowAttributes a;
        int matches=qualifier==NULL;
        for(int i=0;i<count;++i)if(qualifier && !strcmp(arguments[i],qualifier))matches=1;
        if(matches && !strcmp(filename(arguments[0]),command) && XGetWindowAttributes(display,window,&a) && a.map_state==IsViewable)
            found=window;
        XFreeStringList(arguments);
    }
    /* ico sets WM_NAME and WM_PROTOCOLS but deliberately omits WM_COMMAND. */
    if(!found && !strcmp(command,"ico")){
        char *name=NULL;XWindowAttributes a;
        if(XFetchName(display,window,&name) && name){
            if((!strcmp(name,"Ico") || !strncmp(name,"Ico: thread ",12)) &&
               XGetWindowAttributes(display,window,&a) && a.map_state==IsViewable)found=window;
            XFree(name);
        }
    }
    if(found)return found;
    /* Popup-only Xt applications (including xmag) do not put WM_COMMAND
     * on their visible shells. Their WM_CLASS still identifies the client. */
    if(!qualifier){
        XClassHint hint={0};XWindowAttributes a;
        if(XGetClassHint(display,window,&hint)){
            if(hint.res_name && !strcmp(hint.res_name,command) &&
               XGetWindowAttributes(display,window,&a) && a.map_state==IsViewable)found=window;
            if(hint.res_name)XFree(hint.res_name);
            if(hint.res_class)XFree(hint.res_class);
        }
    }
    if(found)return found;
    Window root,parent,*children=NULL;unsigned n=0;
    if(XQueryTree(display,window,&root,&parent,&children,&n))
        for(unsigned i=0;i<n && !found;++i)found=find_window(children[i],command,depth-1);
    if(children)XFree(children);
    return found;
}
static Window canvas(Window window,unsigned depth,unsigned long *area)
{
    if(!depth)return None;
    XWindowAttributes a;Window best=None;
    if(XGetWindowAttributes(display,window,&a) && a.map_state==IsViewable && a.width>80 && a.height>80 &&
       (a.all_event_masks&ButtonPressMask) && (unsigned long)a.width*a.height>*area){
        best=window;*area=(unsigned long)a.width*a.height;
    }
    Window root,parent,*children=NULL;unsigned n=0;
    if(XQueryTree(display,window,&root,&parent,&children,&n))
        for(unsigned i=0;i<n;++i){Window w=canvas(children[i],depth-1,area);if(w)best=w;}
    if(children)XFree(children);
    return best;
}
static int snapshot(Window window)
{
    XWindowAttributes a;if(!XGetWindowAttributes(display,window,&a) || a.map_state!=IsViewable)return -1;
    unsigned width=a.width<64?a.width:64,height=a.height<48?a.height:48;
    if(!width || !height)return -1;
    XImage *image=XGetImage(display,window,0,0,width,height,AllPlanes,ZPixmap);
    if(!image)return -1;
    XDestroyImage(image);return 0;
}
static int locale_check(void)
{
    const char *names[]={"C.UTF-8","en_US.UTF-8","en_US.utf8"};unsigned errors=0;
    for(unsigned i=0;i<3;++i){
        if(!setlocale(LC_ALL,names[i])){fprintf(stderr,"locale: unavailable %s\n",names[i]);++errors;continue;}
        mbstate_t state={0};wchar_t value=0;char encoded[MB_LEN_MAX];
        size_t length=mbrtowc(&value,"\xc3\xb1",2,&state);
        int supported=XSupportsLocale() && XSetLocaleModifiers("")!=NULL;
        int valid=!strcmp(nl_langinfo(CODESET),"UTF-8") && length==2 && value==0xf1 && iswalpha(value);
        memset(&state,0,sizeof(state));
        valid=valid && wcrtomb(encoded,value,&state)==2 && !memcmp(encoded,"\xc3\xb1",2);
        printf("locale: %s CODESET=%s native_Xlib=%d multibyte=%s\n",names[i],nl_langinfo(CODESET),supported,valid?"passed":"failed");
        if(!supported || !valid)++errors;
    }
    printf("locale: completed errors=%u protocol_errors=%u\n",errors,protocol_errors);
    return errors || protocol_errors;
}
static int shaped_readback_check(void)
{
    XSetWindowAttributes attributes={.override_redirect=True,
        .background_pixel=WhitePixel(display,DefaultScreen(display))};
    Window window=XCreateWindow(display,DefaultRootWindow(display),200,200,64,48,0,
        CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel,&attributes);
    XRectangle shape={16,8,32,32};
    XShapeCombineRectangles(display,window,ShapeBounding,0,0,&shape,1,ShapeSet,YXBanded);
    XMapRaised(display,window);XClearWindow(display,window);XSync(display,False);
    const int formats[]={ZPixmap,ZPixmap,XYPixmap};
    const unsigned long masks[]={AllPlanes,0x33,AllPlanes};
    unsigned errors=0,checks=0;
    for(unsigned mode=0;mode<3;++mode){
        XImage *image=XGetImage(display,window,0,0,64,48,masks[mode],formats[mode]);
        if(!image){++errors;continue;}
        for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x){
            unsigned long expected=(x>=16 && x<48 && y>=8 && y<40)?
                WhitePixel(display,DefaultScreen(display))&masks[mode]:0;
            if(XGetPixel(image,x,y)!=expected)++errors;
            ++checks;
        }
        XDestroyImage(image);
    }
    XDestroyWindow(display,window);XSync(display,False);
    printf("shaped-readback: GS censored Z/XY replies pixel_checks=%u errors=%u protocol_errors=%u\n",
           checks,errors,protocol_errors);
    return errors || protocol_errors;
}
static void key(KeySym symbol,int down)
{KeyCode code=XKeysymToKeycode(display,symbol);if(code)XTestFakeKeyEvent(display,code,down,0);}
static int bitmap_actions(Window window)
{
    unsigned long area=0;Window grid=canvas(window,10,&area);XWindowAttributes a;
    if(!grid || !XGetWindowAttributes(display,grid,&a)){fprintf(stderr,"bitmap: editing canvas missing\n");return -1;}
    int x,y;Window child;XTranslateCoordinates(display,grid,DefaultRootWindow(display),a.width/2,a.height/2,&x,&y,&child);
    XTestFakeMotionEvent(display,DefaultScreen(display),x,y,0);XTestFakeButtonEvent(display,1,True,0);
    XTestFakeButtonEvent(display,1,False,0);XSync(display,False);
    XSetInputFocus(display,grid,RevertToParent,CurrentTime);
    key(XK_Control_L,True);key(XK_s,True);key(XK_s,False);key(XK_Control_L,False);XSync(display,False);
    for(unsigned i=0;i<10;++i)pause_tick();
    printf("application: bitmap draw-and-save events sent canvas=%lu\n",grid);return snapshot(grid);
}
static int keyboard_check(void)
{
    if(XkbKeycodeToKeysym(display,51,0,0)!=XK_backslash || XkbKeycodeToKeysym(display,51,0,1)!=XK_bar){
        fprintf(stderr,"keyboard: US key51 does not provide backslash and pipe\n");return 1;
    }
    int fd=open("/dev/tty1",O_RDONLY|O_NOCTTY);if(fd<0){perror("keyboard console");return 1;}
    const unsigned expected[]={92,124};
    for(unsigned shift=0;shift<2;++shift){
        struct kbentry entry={.kb_table=shift,.kb_index=43};
        if(ioctl(fd,KDGKBENT,&entry) || (entry.kb_value!=expected[shift] && entry.kb_value!=(expected[shift]^0xf000))){
            fprintf(stderr,"keyboard: console key43 modifier=%u value=%04x\n",shift,entry.kb_value);close(fd);return 1;
        }
    }
    close(fd);
    Window w=XCreateSimpleWindow(display,DefaultRootWindow(display),30,30,100,60,0,0,0);
    XSetWindowAttributes attributes={.override_redirect=True};XChangeWindowAttributes(display,w,CWOverrideRedirect,&attributes);
    XSelectInput(display,w,KeyPressMask);XMapWindow(display,w);XSync(display,False);
    XSetInputFocus(display,w,RevertToParent,CurrentTime);
    while(XPending(display)){XEvent e;XNextEvent(display,&e);}
    XTestFakeKeyEvent(display,51,True,0);XTestFakeKeyEvent(display,51,False,0);
    key(XK_Shift_L,True);XTestFakeKeyEvent(display,51,True,0);XTestFakeKeyEvent(display,51,False,0);key(XK_Shift_L,False);
    XSync(display,False);unsigned presses=0;
    for(unsigned tick=0;tick<30 && presses<2;++tick){
        while(XPending(display)){
            XEvent e;XNextEvent(display,&e);if(e.type!=KeyPress)continue;
            char text[8];KeySym symbol;int n=XLookupString(&e.xkey,text,sizeof(text),&symbol,NULL);
            if(e.xkey.keycode==51){if(n!=1 || (unsigned char)text[0]!=expected[presses])return 1;++presses;}
        }
        if(presses<2)pause_tick();
    }
    XDestroyWindow(display,w);XSync(display,False);
    printf("keyboard: console key43 and Xorg key51 backslash/pipe presses=%u errors=%u\n",presses,protocol_errors);
    return presses!=2 || protocol_errors;
}
static Window manager(Window window,unsigned depth)
{
    if(!depth)return None;
    char *name=NULL;Window found=None;
    if(XFetchName(display,window,&name) && name){
        if(strstr(name,"Icon Manager"))found=window;
        XFree(name);
    }
    Window root,parent,*children=NULL;unsigned count=0;
    if(!found && XQueryTree(display,window,&root,&parent,&children,&count))
        for(unsigned i=0;i<count && !found;++i)found=manager(children[i],depth-1);
    if(children)XFree(children);
    return found;
}
static int mapped(Window window)
{XWindowAttributes a;return XGetWindowAttributes(display,window,&a) && a.map_state==IsViewable;}
static int state(Window window)
{
    Atom actual;int format;unsigned long count,left;unsigned char *data=NULL;int value=-1;
    if(XGetWindowProperty(display,window,XInternAtom(display,"WM_STATE",False),0,2,False,AnyPropertyType,
        &actual,&format,&count,&left,&data)==Success && count && format==32)value=*(long *)data;
    if(data)XFree(data);
    return value;
}
static int wait_state(Window window,int expected)
{
    for(unsigned i=0;i<100;++i){XSync(display,False);if(state(window)==expected)return 0;pause_tick();}
    return -1;
}
static int bounds(Window window,int *right,int *bottom,int *height)
{
    Window root,parent,*children=NULL,child;unsigned count;XWindowAttributes a;int x,y;
    if(!XQueryTree(display,window,&root,&parent,&children,&count))return -1;
    if(children)XFree(children);
    if(!XGetWindowAttributes(display,parent,&a) ||
       !XTranslateCoordinates(display,parent,DefaultRootWindow(display),-a.border_width,-a.border_width,&x,&y,&child))return -1;
    *right=DisplayWidth(display,DefaultScreen(display))-x-a.width-2*a.border_width;
    *bottom=DisplayHeight(display,DefaultScreen(display))-y-a.height-2*a.border_width;
    *height=a.height;return 0;
}
static int root_capture(const char *path)
{
    Window root=DefaultRootWindow(display);XWindowAttributes a;
    if(!XGetWindowAttributes(display,root,&a))return -1;
    XImage *image=XGetImage(display,root,0,0,a.width,a.height,AllPlanes,ZPixmap);if(!image)return -1;
    uint32_t header[]={0x32534750,image->width,image->height,image->bits_per_pixel,image->bytes_per_line,
        image->red_mask,image->green_mask,image->blue_mask};
    FILE *out=fopen(path,"wb");if(!out){XDestroyImage(image);return -1;}
    int result=fwrite(header,sizeof(header),1,out)!=1 ||
        fwrite(image->data,image->bytes_per_line,image->height,out)!=(size_t)image->height;
    if(fclose(out))result=1;
    XDestroyImage(image);return result?-1:0;
}
static int manager_widths(Window manager_window)
{
    Window root,frame,*rows=NULL,*siblings=NULL;unsigned count=0,sibling_count=0;
    XWindowAttributes manager_attributes,frame_attributes,title_attributes;
    if(!XGetWindowAttributes(display,manager_window,&manager_attributes) ||
       !XQueryTree(display,manager_window,&root,&frame,&rows,&count))return -1;
    int failed=0;unsigned visible_rows=0;
    for(unsigned i=0;i<count;++i){
        XWindowAttributes row;
        if(!XGetWindowAttributes(display,rows[i],&row)){failed=1;continue;}
        if(row.map_state!=IsViewable)continue;
        ++visible_rows;
        if(row.x-row.border_width!=0 ||
           row.width+2*row.border_width!=manager_attributes.width)failed=1;
    }
    if(rows)XFree(rows);
    if(!visible_rows || !XGetWindowAttributes(display,frame,&frame_attributes) ||
       !XQueryTree(display,frame,&root,&manager_window,&siblings,&sibling_count))failed=1;
    unsigned titles=0;
    for(unsigned i=0;i<sibling_count;++i){
        if(!XGetWindowAttributes(display,siblings[i],&title_attributes)){failed=1;continue;}
        if(title_attributes.y<manager_attributes.y && title_attributes.map_state==IsViewable){
            ++titles;
            if(title_attributes.x-title_attributes.border_width!=manager_attributes.x ||
               title_attributes.width+2*title_attributes.border_width!=manager_attributes.width)failed=1;
        }
    }
    if(siblings)XFree(siblings);
    if(titles!=1)failed=1;
    printf("window-list: title and %u rows outer width=%d aligned=%s\n",
           visible_rows,manager_attributes.width,failed?"no":"yes");
    return failed?-1:0;
}
static int manager_title_fit(Window manager_window)
{
    Window root,frame,parent,*children=NULL,*buttons=NULL;unsigned count=0,button_count=0;
    XWindowAttributes manager_attributes,title,button;
    if(!XGetWindowAttributes(display,manager_window,&manager_attributes) ||
       !XQueryTree(display,manager_window,&root,&frame,&children,&count))return -1;
    if(children)XFree(children);
    children=NULL;
    if(!XQueryTree(display,frame,&root,&parent,&children,&count))return -1;
    Window title_window=None;
    for(unsigned i=0;i<count;++i)
        if(XGetWindowAttributes(display,children[i],&title) &&
           title.y<manager_attributes.y && title.map_state==IsViewable)title_window=children[i];
    if(children)XFree(children);
    if(!title_window || !XQueryTree(display,title_window,&root,&parent,&buttons,&button_count))return -1;
    int left_edge=-1,left_end=0,right_start=manager_attributes.width;
    for(unsigned i=0;i<button_count;++i){
        if(!XGetWindowAttributes(display,buttons[i],&button) || button.map_state!=IsViewable ||
           !(button.all_event_masks&ButtonPressMask))continue;
        if(left_edge<0 || button.x<left_edge){left_edge=button.x;left_end=button.x+button.width+2*button.border_width;}
    }
    for(unsigned i=0;i<button_count;++i){
        if(!XGetWindowAttributes(display,buttons[i],&button) || button.map_state!=IsViewable ||
           !(button.all_event_masks&ButtonPressMask))continue;
        if(button.x>left_edge && button.x<right_start)right_start=button.x;
    }
    if(buttons)XFree(buttons);
    char *name=NULL;XFontStruct *font=XLoadQueryFont(display,"7x13bold");
    if(left_edge<0 || !font || !XFetchName(display,manager_window,&name) || !name){
        if(font)XFreeFont(display,font);
        if(name)XFree(name);
        return -1;
    }
    int text_width=XTextWidth(font,name,strlen(name));
    int gap=right_start-(left_end+5+text_width);
    XFreeFont(display,font);XFree(name);
    printf("window-list: fitted title width=%d text_width=%d text-to-buttons gap=%d\n",
           manager_attributes.width,text_width,gap);
    return gap==5?0:-1;
}
/* Compare the visible label with text independently drawn by the GS.  The
 * reference uses the configured font; CPU code only compares readback bits. */
static int manager_label(Window entry,const char *title)
{
    XWindowAttributes a;
    if(!XGetWindowAttributes(display,entry,&a) || a.map_state!=IsViewable || a.width<=23)return -1;
    XFontStruct *font=XLoadQueryFont(display,"6x13");if(!font)return -1;
    unsigned width=a.width-23,height=font->ascent+font->descent;
    Pixmap reference=XCreatePixmap(display,entry,width,height,a.depth);
    GC gc=XCreateGC(display,reference,0,NULL);
    unsigned long black=BlackPixel(display,DefaultScreen(display));
    XSetForeground(display,gc,black);XFillRectangle(display,reference,gc,0,0,width,height);
    XSetForeground(display,gc,WhitePixel(display,DefaultScreen(display)));
    XSetFont(display,gc,font->fid);
    XDrawString(display,reference,gc,0,font->ascent,title,strlen(title));
    XImage *expected=XGetImage(display,reference,0,0,width,height,AllPlanes,ZPixmap);
    XImage *actual=XGetImage(display,entry,0,0,a.width,a.height,AllPlanes,ZPixmap);
    int result=-1;
    if(expected && actual){
        unsigned long background=XGetPixel(actual,1,1);result=0;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            if((XGetPixel(actual,x+22,y+4)!=background)!=(XGetPixel(expected,x,y)!=black))result=1;
    }
    if(expected)XDestroyImage(expected);
    if(actual)XDestroyImage(actual);
    XFreeGC(display,gc);XFreePixmap(display,reference);XFreeFont(display,font);
    return result;
}
static int wait_manager_label(Window entry,const char *title)
{
    for(unsigned tick=0;tick<50;++tick){
        if(!manager_label(entry,title)){
            printf("window-list: visible GS label matches title=\"%s\"\n",title);return 0;
        }
        pause_tick();
    }
    fprintf(stderr,"window-list: visible label differs from title=\"%s\"\n",title);return -1;
}
static int manager_default_labels(Window m)
{
    Window root,parent,*rows=NULL;unsigned count=0;
    const char *programs[]={"xclock","xterm"};int result=0;
    if(!XQueryTree(display,m,&root,&parent,&rows,&count))return -1;
    for(unsigned p=0;p<2;++p){
        Window w=find_window(DefaultRootWindow(display),programs[p],16);char *title=NULL,*icon=NULL;
        if(!w || !XFetchName(display,w,&title) || !title){result=-1;break;}
        XGetIconName(display,w,&icon);
        unsigned matches=0;
        for(unsigned r=0;r<count;++r)if(!manager_label(rows[r],title))++matches;
        printf("window-list: %s title=\"%s\" icon=\"%s\" matching_rows=%u\n",
               programs[p],title,icon?icon:"",matches);
        if(matches!=1)result=-1;
        XFree(title);if(icon)XFree(icon);
    }
    if(rows)XFree(rows);
    return result;
}
static int manager_entry_order(Window m,Window entry,int first)
{
    Window root,parent,*rows=NULL;unsigned count=0;XWindowAttributes target;
    if(!XGetWindowAttributes(display,entry,&target) || !XQueryTree(display,m,&root,&parent,&rows,&count))return -1;
    int result=0;
    for(unsigned i=0;i<count;++i){
        XWindowAttributes row;
        if(!XGetWindowAttributes(display,rows[i],&row)){result=-1;break;}
        if(row.map_state==IsViewable && (first?row.y<target.y:row.y>target.y))result=-1;
    }
    if(rows)XFree(rows);
    printf("window-list: title sorting at %s row=%s\n",first?"first":"last",result?"failed":"passed");
    return result;
}
static int window_list_check(const char *capture)
{
    Window m=manager(DefaultRootWindow(display),16);int right,bottom,height;
    if(!m || !mapped(m) || bounds(m,&right,&bottom,&height) || right!=12 || bottom!=12){
        fprintf(stderr,"window-list: visible bottom-right manager missing\n");return 1;
    }
    if(manager_widths(m) || manager_title_fit(m) || manager_default_labels(m))return 1;
    Window root,parent,*old=NULL,*new_children=NULL;unsigned old_count=0,new_count=0;
    XQueryTree(display,m,&root,&parent,&old,&old_count);
    Window test=XCreateSimpleWindow(display,DefaultRootWindow(display),80,180,200,100,0,0,WhitePixel(display,DefaultScreen(display)));
    XStoreName(display,test,"Zulu window title");XSetIconName(display,test,"AAA icon name");
    XClassHint hint={"ps2-panel-test","PS2PanelTest"};XSetClassHint(display,test,&hint);
    XMapWindow(display,test);XSync(display,False);
    if(wait_state(test,NormalState))return 1;
    Window entry=None;
    for(unsigned tick=0;tick<100 && !entry;++tick){
        XQueryTree(display,m,&root,&parent,&new_children,&new_count);
        for(unsigned i=0;i<new_count;++i){
            unsigned j=0;while(j<old_count && old[j]!=new_children[i])++j;
            if(j==old_count)entry=new_children[i];
        }
        if(new_children)XFree(new_children);
        new_children=NULL;
        if(!entry)pause_tick();
    }
    if(old)XFree(old);
    int next_height;
    if(!entry || bounds(m,&right,&bottom,&next_height) || right!=12 || bottom!=12 || next_height<=height || manager_widths(m))return 1;
    if(wait_manager_label(entry,"Zulu window title") || manager_entry_order(m,entry,0))return 1;
    XStoreName(display,test,"Aardvark window title");XSync(display,False);
    if(wait_manager_label(entry,"Aardvark window title") || manager_entry_order(m,entry,1))return 1;
    XSetIconName(display,test,"ZZZ different icon name");XSync(display,False);
    for(unsigned tick=0;tick<5;++tick)pause_tick();
    if(wait_manager_label(entry,"Aardvark window title") || manager_entry_order(m,entry,1))return 1;
    XIconifyWindow(display,test,DefaultScreen(display));XSync(display,False);
    if(wait_state(test,IconicState) || !mapped(entry) || manager_widths(m))return 1;
    XStoreName(display,test,"Updated minimized title");XSync(display,False);
    if(wait_manager_label(entry,"Updated minimized title") || manager_entry_order(m,entry,0) || root_capture(capture))return 1;
    XWindowAttributes a;int x,y;Window child;
    if(!XGetWindowAttributes(display,entry,&a) ||
       !XTranslateCoordinates(display,entry,DefaultRootWindow(display),a.width/2,a.height/2,&x,&y,&child))return 1;
    XTestFakeMotionEvent(display,DefaultScreen(display),x,y,0);
    XTestFakeButtonEvent(display,1,True,0);XTestFakeButtonEvent(display,1,False,0);XSync(display,False);
    if(wait_state(test,NormalState) || !mapped(test))return 1;
    XDestroyWindow(display,test);XSync(display,False);
    for(unsigned tick=0;tick<100;++tick){
        if(!bounds(m,&right,&bottom,&next_height) && next_height==height)break;
        pause_tick();
    }
    if(right!=12 || bottom!=12 || next_height!=height || manager_widths(m))return 1;
    puts("window-list: bottom-right growth, minimized entry, click-to-restore and shrink passed");
    return protocol_errors?1:0;
}
int main(int argc,char **argv)
{
    setbuf(stdout,NULL);setbuf(stderr,NULL);
    if(argc<2 || !(display=XOpenDisplay(NULL))){fprintf(stderr,"application: cannot connect to Xorg\n");return 2;}
    XSetErrorHandler(xerror);
    if(!strcmp(argv[1],"keyboard")){int result=keyboard_check();XCloseDisplay(display);return result;}
    if(!strcmp(argv[1],"locale")){int result=locale_check();XCloseDisplay(display);return result;}
    if(!strcmp(argv[1],"shaped-readback")){int result=shaped_readback_check();XCloseDisplay(display);return result;}
    if(argc==3 && !strcmp(argv[1],"window-list")){int result=window_list_check(argv[2]);XCloseDisplay(display);return result;}
    if(argc<4 || strcmp(argv[1],"gui"))return 2;
    const char *label=argv[2],*command=filename(argv[3]);
    if(!strcmp(command,"uxterm"))command="xterm";
    char unique[80];snprintf(unique,sizeof(unique),"ps2-suite-%s",label);
    if(!strcmp(command,"xterm") || !strcmp(command,"xclock"))qualifier=unique;
    signal(SIGTERM,interrupted);signal(SIGINT,interrupted);
    pid_t app=fork();if(app<0)return 2;
    if(!app){
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
        close(ConnectionNumber(display));setpgid(0,0);execvp(argv[3],argv+3);perror(argv[3]);_exit(127);
    }
    child_group=app;setpgid(app,app);
    Window window=None;int status=0,exited=0;
    for(unsigned tick=0;tick<150;++tick){
        if(waitpid(app,&status,WNOHANG)==app){exited=1;break;}
        window=find_window(DefaultRootWindow(display),command,16);
        if(window)break;
        pause_tick();
    }
    int result=0;
    if(!window){fprintf(stderr,"application: %s did not map, exited=%d status=%d\n",label,exited,status);result=1;}
    else{
        XWindowAttributes before;XGetWindowAttributes(display,window,&before);
        printf("application: mapped %s window=%lu dimensions=%dx%d\n",label,window,before.width,before.height);
        if(snapshot(window))result=1;
        unsigned width=before.width>700?700:before.width,height=before.height>500?500:before.height;
        if(width>60 && height>60){XResizeWindow(display,window,width-7,height-5);XSync(display,False);}
        XClearArea(display,window,0,0,0,0,True);XSync(display,False);
        for(unsigned i=0;i<10;++i)pause_tick();
        if(!strcmp(label,"bitmap") && bitmap_actions(window))result=1;
        if(snapshot(window))result=1;
        Atom protocols=XInternAtom(display,"WM_PROTOCOLS",False),delete=XInternAtom(display,"WM_DELETE_WINDOW",False);
        XEvent event={0};event.xclient.type=ClientMessage;event.xclient.display=display;
        event.xclient.window=window;event.xclient.message_type=protocols;event.xclient.format=32;
        event.xclient.data.l[0]=delete;event.xclient.data.l[1]=CurrentTime;
        XSendEvent(display,window,False,NoEventMask,&event);XSync(display,False);
        for(unsigned tick=0;tick<20;++tick){
            if(waitpid(app,&status,WNOHANG)==app){exited=1;break;}
            pause_tick();
        }
        /* xmessage documents WM_DELETE_WINDOW as exit(1), a normal dismissal. */
        int dismissed=!strcmp(command,"xmessage") && WIFEXITED(status) && WEXITSTATUS(status)==1;
        if(exited && (!WIFEXITED(status) || (WEXITSTATUS(status) && !dismissed))){
            fprintf(stderr,"application: %s exited abnormally status=%d\n",label,status);result=1;
        }
    }
    if(!exited){
        kill(-app,SIGTERM);int ended=0;
        for(unsigned tick=0;tick<15;++tick){if(waitpid(app,&status,WNOHANG)==app){ended=1;break;}pause_tick();}
        if(!ended){kill(-app,SIGKILL);while(waitpid(app,&status,0)<0 && errno==EINTR){};result=1;}
    }
    child_group=0;XSync(display,False);
    printf("application: completed %s result=%s protocol_errors=%u close=%s\n",label,result || protocol_errors?"failed":"passed",protocol_errors,exited?"client":"bounded SIGTERM");
    XCloseDisplay(display);return result || protocol_errors;
}
