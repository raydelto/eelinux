/* Actual pointer/button and ICCCM checks against the patched TWM on Xvfb. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;
static Window root;
static Atom wm_state, wm_delete;
static int errors;
static int error_handler(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;
    ++errors;
    return 0;
}
#define CHECK(test, message) do { if (!(test)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

static void settle(void)
{
    XSync(dpy, False);
    usleep(120000);
    XSync(dpy, False);
}

static XWindowAttributes attrs(Window w)
{
    XWindowAttributes a;
    memset(&a, 0, sizeof(a));
    XGetWindowAttributes(dpy, w, &a);
    return a;
}

static Window parent(Window w)
{
    Window r, p, *children = NULL;
    unsigned n;
    if (!XQueryTree(dpy, w, &r, &p, &children, &n))
        return None;
    if (children) XFree(children);
    return p;
}

static Window frame_of(Window w)
{
    for (int i = 0; i < 150; ++i) {
        Window p = parent(w);
        if (p != None && p != root && attrs(p).map_state == IsViewable)
            return p;
        usleep(20000);
    }
    CHECK(0, "client not managed/mapped");
    return None;
}

static Window title_of(Window frame, Window client)
{
    Window r, p, *children = NULL, title = None;
    unsigned n;
    XQueryTree(dpy, frame, &r, &p, &children, &n);
    for (unsigned i = 0; i < n; ++i) {
        XWindowAttributes a = attrs(children[i]);
        if (children[i] != client && a.height < 70 && a.width > 90)
            title = children[i];
    }
    if (children) XFree(children);
    CHECK(title != None, "title bar exists");
    return title;
}

static Window button(Window frame, Window client, int index)
{
    Window title = title_of(frame, client), r, p, *children = NULL;
    Window buttons[4];
    int positions[4], count = 0;
    unsigned n;
    XQueryTree(dpy, title, &r, &p, &children, &n);
    for (unsigned i = 0; i < n; ++i) {
        XWindowAttributes a = attrs(children[i]);
        if (a.width == a.height && a.width > 4 && count < 4) {
            buttons[count] = children[i];
            positions[count++] = a.x;
        }
    }
    if (children) XFree(children);
    CHECK(count == 4, "four controller buttons exist");
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
            if (positions[j] < positions[i]) {
                int x = positions[i]; positions[i] = positions[j]; positions[j] = x;
                Window w = buttons[i]; buttons[i] = buttons[j]; buttons[j] = w;
            }
    return buttons[index];
}

static void pointer(Window w, int x, int y)
{
    int rx, ry;
    Window child;
    XTranslateCoordinates(dpy, w, root, x, y, &rx, &ry, &child);
    XTestFakeMotionEvent(dpy, DefaultScreen(dpy), rx, ry, CurrentTime);
    XFlush(dpy);
    usleep(20000);
}

static void click(Window w)
{
    XWindowAttributes a = attrs(w);
    pointer(w, a.width / 2, a.height / 2);
    XTestFakeButtonEvent(dpy, Button1, True, CurrentTime);
    XFlush(dpy);
    usleep(20000);
    XTestFakeButtonEvent(dpy, Button1, False, CurrentTime);
    settle();
}

static void action(Window frame, Window client, int index)
{
    click(button(frame, client, index));
}

static void drag(Window w, int x, int y, int dx, int dy, int mouse, int alt)
{
    int rx, ry;
    Window child;
    XTranslateCoordinates(dpy, w, root, x, y, &rx, &ry, &child);
    XTestFakeMotionEvent(dpy, DefaultScreen(dpy), rx, ry, CurrentTime);
    if (alt) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), True, CurrentTime);
    XTestFakeButtonEvent(dpy, (unsigned)mouse, True, CurrentTime);
    XFlush(dpy);
    usleep(50000);
    for (int i = 1; i <= 5; ++i) {
        XTestFakeMotionEvent(dpy, DefaultScreen(dpy), rx + dx * i / 5,
                            ry + dy * i / 5, CurrentTime);
        XFlush(dpy);
        usleep(20000);
    }
    XTestFakeButtonEvent(dpy, (unsigned)mouse, False, CurrentTime);
    if (alt) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), False, CurrentTime);
    settle();
}

static long state(Window client)
{
    Atom actual;
    int format;
    unsigned long n, remaining;
    unsigned char *data = NULL;
    long result = -1;
    if (XGetWindowProperty(dpy, client, wm_state, 0, 2, False, wm_state,
                          &actual, &format, &n, &remaining, &data) == Success && data && n)
        result = *(long *)data;
    if (data) XFree(data);
    return result;
}

static Window named(Window start, const char *needle)
{
    char *name = NULL;
    if (XFetchName(dpy, start, &name) && name) {
        int match = strstr(name, needle) != NULL;
        XFree(name);
        if (match) return start;
    }
    Window r, p, *children = NULL, found = None;
    unsigned n;
    if (XQueryTree(dpy, start, &r, &p, &children, &n)) {
        for (unsigned i = 0; i < n && !found; ++i)
            found = named(children[i], needle);
        if (children) XFree(children);
    }
    return found;
}

static Window create(Display *d, const char *name, int protocols)
{
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 60, 50, 320, 180,
                                   0, 0, WhitePixel(d, DefaultScreen(d)));
    XSizeHints hints = {0};
    hints.flags = PPosition | PMinSize | PResizeInc;
    hints.x = 60; hints.y = 50;
    hints.min_width = 96; hints.min_height = 60;
    hints.width_inc = 8; hints.height_inc = 6;
    XSetWMNormalHints(d, w, &hints);
    XStoreName(d, w, name);
    XSelectInput(d, w, StructureNotifyMask | PropertyChangeMask);
    if (protocols) XSetWMProtocols(d, w, &wm_delete, 1);
    XMapWindow(d, w);
    XFlush(d);
    return w;
}

static unsigned long pixels(Window w)
{
    XWindowAttributes a = attrs(w);
    XImage *image = XGetImage(dpy, w, 0, 0, (unsigned)a.width, (unsigned)a.height,
                             AllPlanes, ZPixmap);
    CHECK(image, "read icon-manager labels");
    unsigned long hash = 5381;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x)
            hash = (hash * 33) ^ XGetPixel(image, x, y);
    XDestroyImage(image);
    return hash;
}

/* TWM's menu and shadow windows are direct root children. Only the menu
 * receives enter/expose events; managed frames redirect substructure events. */
static Window menu_window(void)
{
    Window r, p, *children = NULL, result = None;
    unsigned n;
    XQueryTree(dpy, root, &r, &p, &children, &n);
    for (unsigned i = n; i > 0; --i) {
        XWindowAttributes a = attrs(children[i - 1]);
        if (a.map_state == IsViewable && a.width > 40 && a.height > 20 &&
            (a.all_event_masks & EnterWindowMask) &&
            (a.all_event_masks & ExposureMask) &&
            !(a.all_event_masks & SubstructureRedirectMask)) {
            result = children[i - 1];
            break;
        }
    }
    if (children) XFree(children);
    return result;
}

static Window open_root_menu(unsigned mouse)
{
    pointer(root, 700, 110);
    XTestFakeButtonEvent(dpy, mouse, True, CurrentTime);
    XFlush(dpy);
    usleep(80000);
    XTestFakeButtonEvent(dpy, mouse, False, CurrentTime);
    settle();
    Window menu = menu_window();
    CHECK(menu != None, "root menu survives opening button release");
    return menu;
}

static void menu_click(Window menu, int item, int count)
{
    XWindowAttributes a = attrs(menu);
    pointer(menu, a.width / 4, (2 * item + 1) * a.height / (2 * count));
    XTestFakeButtonEvent(dpy, Button1, True, CurrentTime);
    XFlush(dpy);
    usleep(20000);
    XTestFakeButtonEvent(dpy, Button1, False, CurrentTime);
    settle();
}

static Window open_context_menu(Window window)
{
    XWindowAttributes a = attrs(window);
    pointer(window, a.width / 2, a.height < 70 ? 8 : a.height / 2);
    XTestFakeButtonEvent(dpy, Button3, True, CurrentTime);
    XFlush(dpy);
    usleep(80000);
    XTestFakeButtonEvent(dpy, Button3, False, CurrentTime);
    settle();
    Window menu = menu_window();
    CHECK(menu, "context menu survives right button release");
    return menu;
}

static void check_menu_grabs_released(void)
{
    CHECK(XGrabPointer(dpy, root, False, ButtonPressMask, GrabModeAsync,
                       GrabModeAsync, None, None, CurrentTime) == GrabSuccess,
          "menu releases pointer grab");
    XUngrabPointer(dpy, CurrentTime);
    CHECK(XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
                        CurrentTime) == GrabSuccess, "menu releases keyboard grab");
    XUngrabKeyboard(dpy, CurrentTime);
    XSync(dpy, False);
}

static void root_menu_checks(void)
{
    Window client = create(dpy, "Z Sticky Menu Client", 1);
    Window frame = frame_of(client);
    Window manager = named(root, "Icon Manager");
    Window manager_frame = parent(manager);
    Window menu = open_root_menu(Button1);
    unsigned long unselected = pixels(menu);
    XWindowAttributes a = attrs(menu);
    pointer(menu, a.width / 4, 11 * a.height / 14);
    settle();
    CHECK(pixels(menu) != unselected, "root menu follows pointer after release");
    menu_click(menu, 5, 7);
    CHECK(attrs(manager_frame).map_state == IsUnmapped && menu_window() == None,
          "left root menu executes clicked entry and closes");
    menu = open_root_menu(Button1);
    menu_click(menu, 4, 7);
    CHECK(attrs(manager_frame).map_state == IsViewable, "menu can reopen after selection");
    check_menu_grabs_released();

    XIconifyWindow(dpy, client, DefaultScreen(dpy));
    settle();
    CHECK(state(client) == IconicState, "window starts minimized for right menu test");
    menu = open_root_menu(Button3);
    menu_click(menu, 2, 3); /* title, TWM Icon Manager, Z Sticky Menu Client */
    CHECK(state(client) == NormalState && attrs(frame).map_state == IsViewable &&
          menu_window() == None, "right root menu restores clicked window");

    menu = open_root_menu(Button1);
    menu_click(menu, 2, 7); /* Click the label, outside its hover arrow. */
    CHECK(attrs(menu).map_state == IsViewable && menu_window() != menu,
          "clicking a submenu entry opens its child");
    menu_click(menu_window(), 1, 2);
    CHECK(attrs(manager_frame).map_state == IsUnmapped && menu_window() == None,
          "submenu click executes once and closes the full chain");
    menu = open_root_menu(Button1);
    menu_click(menu, 4, 7);

    for (unsigned mouse = Button1; mouse <= Button3; mouse += 2) {
        menu = open_root_menu(mouse);
        pointer(root, 20, 520);
        XTestFakeButtonEvent(dpy, mouse, True, CurrentTime);
        XTestFakeButtonEvent(dpy, mouse, False, CurrentTime);
        settle();
        CHECK(menu_window() == None && attrs(manager_frame).map_state == IsViewable,
              "outside click cancels root menu without executing an entry");
        check_menu_grabs_released();
        menu = open_root_menu(mouse);
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Escape), True, CurrentTime);
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Escape), False, CurrentTime);
        settle();
        CHECK(menu_window() == None, "Escape cancels root menu");
        check_menu_grabs_released();
    }

    /* Even a drag onto a command on the opening press must only open it. */
    pointer(root, 700, 110);
    XTestFakeButtonEvent(dpy, Button1, True, CurrentTime);
    XFlush(dpy);
    usleep(80000);
    menu = menu_window();
    CHECK(menu, "root menu opens on press");
    a = attrs(menu);
    pointer(menu, a.width / 4, 11 * a.height / 14);
    XTestFakeButtonEvent(dpy, Button1, False, CurrentTime);
    settle();
    CHECK(attrs(menu).map_state == IsViewable && attrs(manager_frame).map_state == IsViewable,
          "opening release never activates the hovered command");
    menu_click(menu, 0, 7);
    CHECK(attrs(menu).map_state == IsViewable, "clicking a menu title keeps it open");
    XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Escape), True, CurrentTime);
    XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Escape), False, CurrentTime);
    settle();
    check_menu_grabs_released();

    for (int context = 0; context < 2; ++context) {
        Window invoking = context == 0 ? title_of(frame, client) : manager;
        menu = open_context_menu(invoking);
        menu_click(menu, 5, 9);
        CHECK(attrs(frame).height < 70 && state(client) == NormalState,
              "context menu applies shade to its original window");
        menu = open_context_menu(invoking);
        menu_click(menu, 5, 9);
        CHECK(attrs(frame).height >= 70, "context menu applies unshade");
        menu = open_context_menu(invoking);
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Escape), True, CurrentTime);
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Escape), False, CurrentTime);
        settle();
        CHECK(menu_window() == None, "Escape cancels title/icon-manager context menu");
        check_menu_grabs_released();
        menu = open_context_menu(invoking);
        pointer(root, 20, 520);
        XTestFakeButtonEvent(dpy, Button1, True, CurrentTime);
        XTestFakeButtonEvent(dpy, Button1, False, CurrentTime);
        settle();
        CHECK(menu_window() == None, "outside click cancels title/icon-manager menu");
        check_menu_grabs_released();
    }
    a = attrs(client);
    drag(client, a.width * 3 / 4, a.height * 3 / 4, 32, 24, Button3, 1);
    CHECK(attrs(client).width > a.width && attrs(client).height > a.height,
          "Alt-right direct resize still works after context menus");
    XDestroyWindow(dpy, client);
    settle();
    puts("TWM root menus: click, release, hover, submenu, selection and cancellation passed");
}

int main(void)
{
    dpy = XOpenDisplay(NULL);
    CHECK(dpy, "connect Xvfb");
    XSetErrorHandler(error_handler);
    /* Real input continues while TWM grabs the server during outline resize.
     * Let this synthetic input connection behave the same way. */
    XTestGrabControl(dpy, True);
    root = DefaultRootWindow(dpy);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_state = XInternAtom(dpy, "WM_STATE", False);
    Window client = create(dpy, "TWM Controls Test", 1), frame = frame_of(client);
    XWindowAttributes original = attrs(client), full = attrs(frame), a;
    CHECK(state(client) == NormalState, "initial ICCCM NormalState");

    Window manager = named(root, "Icon Manager");
    CHECK(manager, "window list exists");
    Window manager_frame = parent(manager);
    XWindowAttributes anchored = attrs(manager_frame);
    Window extra = create(dpy, "Another Window", 1);
    frame_of(extra);
    settle();
    a = attrs(manager_frame);
    CHECK(a.x + a.width == anchored.x + anchored.width &&
          a.y + a.height == anchored.y + anchored.height && a.height > anchored.height,
          "window list grows at its configured right/bottom anchors");
    XDestroyWindow(dpy, extra);
    settle();

    action(frame, client, 0);
    a = attrs(frame);
    CHECK(a.height < 70 && a.width == full.width, "triangle shows only title");
    CHECK(attrs(client).height == original.height && attrs(client).map_state == IsViewable,
          "shade preserves mapped client and body geometry");
    CHECK(state(client) == NormalState, "shade does not minimize/withdraw");

    XResizeWindow(dpy, client, 352, 198);
    settle();
    CHECK(attrs(frame).height < 70, "client resize remains shaded");
    action(frame, client, 0);
    CHECK(attrs(client).width == 352 && attrs(client).height == 198,
          "unshade restores latest requested body size");

    action(frame, client, 0);
    a = attrs(frame);
    Window title = title_of(frame, client);
    drag(title, attrs(title).width / 2, 8, 40, 30, Button1, 0);
    CHECK(attrs(frame).x == a.x + 40 && attrs(frame).y == a.y + 30,
          "shaded title drag moves window");
    CHECK(attrs(frame).height < 70, "move retains shade");
    action(frame, client, 0);
    full = attrs(frame);

    action(frame, client, 2);
    a = attrs(frame);
    CHECK(a.x == 0 && a.y == 0 && a.width >= 780 && a.height >= 570,
          "circle maximizes within display");
    CHECK(a.width + 2 * a.border_width <= 800 && a.height + 2 * a.border_width <= 600,
          "maximize respects screen bounds and resize hints");
    action(frame, client, 0);
    action(frame, client, 2);
    a = attrs(frame);
    CHECK(a.x == full.x && a.y == full.y && a.width == full.width && a.height == full.height,
          "circle restores pre-maximize geometry even after shading");

    a = attrs(frame);
    original = attrs(client);
    drag(frame, a.width + 1, a.height + 1, 40, 30, Button1, 0);
    CHECK(attrs(client).width > original.width && attrs(client).height > original.height,
          "left drag on frame corner resizes");
    CHECK((attrs(client).width - 96) % 8 == 0 && (attrs(client).height - 60) % 6 == 0,
          "border resize honors client increments");
    original = attrs(client);
    drag(client, original.width * 3 / 4, original.height * 3 / 4, 32, 24, Button3, 1);
    CHECK(attrs(client).width > original.width && attrs(client).height > original.height,
          "Alt-right drag inside body resizes immediately");

    action(frame, client, 0);
    action(frame, client, 1);
    CHECK(attrs(frame).map_state == IsUnmapped && state(client) == IconicState,
          "square minimizes using ICCCM IconicState");
    unsigned long old_label = pixels(manager);
    XStoreName(dpy, client, "A Changed Window Title");
    settle();
    CHECK(pixels(manager) != old_label, "window list refreshes titles while minimized");
    click(manager);
    CHECK(attrs(frame).map_state == IsViewable && state(client) == NormalState,
          "window list restores minimized client");
    CHECK(attrs(frame).height < 70, "minimize and restore retain shade");
    action(frame, client, 0);

    /* Shaped clients must recover their original nonrectangular frame. */
    XRectangle shape = {0, 0, 200, 100};
    XShapeCombineRectangles(dpy, client, ShapeBounding, 0, 0, &shape, 1, ShapeSet, Unsorted);
    settle();
    action(frame, client, 0);
    action(frame, client, 0);
    Bool bounding, clip;
    int bx, by, cx, cy;
    unsigned bw, bh, cw, ch;
    XShapeQueryExtents(dpy, frame, &bounding, &bx, &by, &bw, &bh,
                      &clip, &cx, &cy, &cw, &ch);
    CHECK(bounding && bh >= 100 && attrs(client).width > 200,
          "shaped frame restored after shade cycle");

    while (XPending(dpy)) { XEvent event; XNextEvent(dpy, &event); }
    action(frame, client, 3);
    int got_delete = 0;
    while (XPending(dpy)) {
        XEvent event;
        XNextEvent(dpy, &event);
        if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == wm_delete)
            got_delete = 1;
    }
    CHECK(got_delete && attrs(client).map_state == IsViewable,
          "X asks protocol-aware clients to close gracefully");
    XDestroyWindow(dpy, client);
    settle();

    Display *legacy = XOpenDisplay(NULL);
    CHECK(legacy, "separate legacy client connection");
    Window old_client = create(legacy, "Legacy Close Test", 0);
    Window old_frame = frame_of(old_client);
    action(old_frame, old_client, 3);
    errors = 0;
    attrs(old_client);
    XSync(dpy, False);
    CHECK(errors > 0, "X closes clients without WM_DELETE_WINDOW");
    /* The server closed legacy's connection; process exit releases its storage. */
    root_menu_checks();
    XCloseDisplay(dpy);
    puts("TWM controls: all checks passed");
    return 0;
}
