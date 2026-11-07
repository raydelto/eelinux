/* SPDX-License-Identifier: MIT */
/* Static resolution-scaled X11 artwork; no resident process or animation. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <string.h>

static unsigned long colour(Display *d, int screen, int r, int g, int b)
{
    XColor c = { .red = r * 257, .green = g * 257, .blue = b * 257,
                 .flags = DoRed | DoGreen | DoBlue };
    if (!XAllocColor(d, DefaultColormap(d, screen), &c)) return BlackPixel(d, screen);
    return c.pixel;
}

static int ignore_stale_pixmap(Display *d, XErrorEvent *event)
{ (void)d; (void)event; return 0; }

/* The private property identifies this helper's sole retained resource.
 * Repaint it in place on subsequent calls instead of retaining another
 * screen-sized GS allocation. Keep _XROOTPMAP_ID valid for other clients. */
static Pixmap previous_background(Display *d, Window root, Atom property,
                                  unsigned *width, unsigned *height, unsigned *depth)
{
    Atom type; int format; unsigned long count, after;
    unsigned char *data = NULL; Pixmap pixmap = None;
    if (XGetWindowProperty(d, root, property, 0, 1, False, XA_PIXMAP,
            &type, &format, &count, &after, &data) == Success &&
        type == XA_PIXMAP && format == 32 && count == 1 && !after && data)
        pixmap = *(unsigned long *)data;
    if (data) XFree(data);
    if (!pixmap) return None;
    Window owner_root; int x, y; unsigned border;
    XSync(d, False);
    XErrorHandler handler = XSetErrorHandler(ignore_stale_pixmap);
    Status valid = XGetGeometry(d, pixmap, &owner_root, &x, &y,
                                width, height, &border, depth);
    XSync(d, False); XSetErrorHandler(handler);
    return valid && owner_root == root ? pixmap : None;
}

int main(void)
{
    Display *d = XOpenDisplay(NULL);
    Window root;
    Pixmap p;
    GC gc;
    Atom prop, retained_property;
    Pixmap previous; unsigned old_width = 0, old_height = 0, old_depth = 0;
    int screen, w, h, y, i;
    const char *heading = "P S 2  /  E M O T I O N   E N G I N E";
    const char *hint = "eeLinux  |  F1 menu  |  Alt+Enter terminal";
    if (!d) { fprintf(stderr, "Cannot open the X display.\n"); return 1; }
    screen = DefaultScreen(d); root = RootWindow(d, screen);
    w = DisplayWidth(d, screen); h = DisplayHeight(d, screen);
    retained_property = XInternAtom(d, "_EELINUX_BACKGROUND_PIXMAP", False);
    previous = previous_background(d, root, retained_property,
                                    &old_width, &old_height, &old_depth);
    int reuse = previous && old_width == (unsigned)w && old_height == (unsigned)h &&
                old_depth == (unsigned)DefaultDepth(d, screen);
    p = reuse ? previous : XCreatePixmap(d, root, w, h, DefaultDepth(d, screen));
    gc = XCreateGC(d, p, 0, NULL);
    for (i = 0; i < 32; ++i) {
        y = i * h / 32;
        XSetForeground(d, gc, colour(d, screen, 3 + i/8, 8 + i/3, 22 + i));
        XFillRectangle(d, p, gc, 0, y, w, (i+1)*h/32-y+1);
    }
    /* Quiet glass towers reference the console's startup skyline. */
    for (i = 0; i < 9; ++i) {
        int x = w * (45 + i*5) / 100, top = h * (30 + (i*17)%29) / 100;
        int width = w / 24, bottom = h * 88 / 100;
        XPoint side[4] = {{x+width,top}, {x+width+w/90,top-h/90},
                         {x+width+w/90,bottom-h/90}, {x+width,bottom}};
        XSetForeground(d, gc, colour(d, screen, 11+i, 25+i*2, 53+i*3));
        XFillRectangle(d, p, gc, x, top, width, bottom-top);
        XSetForeground(d, gc, colour(d, screen, 18+i, 43+i*2, 81+i*2));
        XFillPolygon(d, p, gc, side, 4, Convex, CoordModeOrigin);
        XSetForeground(d, gc, colour(d, screen, 39, 82, 131));
        XDrawLine(d, p, gc, x, top, x+width, top);
    }
    XSetForeground(d, gc, colour(d, screen, 38, 88, 136));
    for (i = 0; i < 44; ++i)
        XDrawPoint(d, p, gc, ((i*173+37)%997)*w/997, ((i*89+31)%601)*h/760);
    XSetForeground(d, gc, colour(d, screen, 113, 218, 255));
    XDrawString(d, p, gc, w/24, h-40, heading, (int)strlen(heading));
    XSetForeground(d, gc, colour(d, screen, 133, 157, 194));
    XDrawString(d, p, gc, w/24, h-21, hint, (int)strlen(hint));
    XSetWindowBackgroundPixmap(d, root, p);
    prop = XInternAtom(d, "_XROOTPMAP_ID", False);
    XChangeProperty(d, root, prop, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&p, 1);
    XChangeProperty(d, root, retained_property, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&p, 1);
    XClearWindow(d, root);
    XFreeGC(d, gc);
    if (!reuse) {
        if (previous) XKillClient(d, previous);
        XSetCloseDownMode(d, RetainPermanent);
    }
    XCloseDisplay(d);
    return 0;
}
