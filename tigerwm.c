/*
                         __,,,,_
          _ __..-;''`--/'/ /.',-`-.
      (`/' ` |  \ \ \\ / / / / .-'/`,_
     /'\ \   |  \ | \| // // / -.,/_,'-,
    /<7' ;  \ \  | ; ||/ /| | \/    |`-/,/-.,_,/')
   /  _.-, `,-\,__|  _-| / \ \/|_/  |    '-/.;.\'  
   `-`  f/ ;      / __/ \__ `/ |__/ |
        `-'      |  -| =|\_  \  |-' |
              __/   /_..-' `  ),'  //
             ((__.-'((___..-'' \__.'     
*/

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include "config.h"

// keycode to modifier mapping
typedef struct Key Key;
struct Key{
    unsigned int mod;
    KeySym keysym;
    void (*function)(const Arg arg);
    const Arg arg;
};

typedef union {
    const char** com;
    const int i;
} Arg;

// Structs
struct key {
    unsigned int mod;
    KeySym keysym;
    void (*function)(const Arg arg);
    const Arg arg;
};

typedef struct client client;
struct client{
    // Prev and next client
    client *next;
    client *prev;

    // Xlib window
    Window win;
};

typedef struct desktop desktop;
struct desktop{
    int master_size;
    int mode;
    client *head;
    client *current;
};

// Functions
static void add_window(Window w);
static void change_desktop(const Arg arg);
static void client_to_desktop(const Arg arg);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static void decrease();
static void destroynotify(XEvent *e);
static void die(const char* e);
static unsigned long getcolor(const char* color);
static void grabkeys();
static void increase();
static void keypress(XEvent *e);
static void kill_client();
static void maprequest(XEvent *e);
static void move_down();
static void move_up();
static void next_desktop();
static void next_win();
static void prev_desktop();
static void prev_win();
static void quit();
static void resizemaster(const Arg arg);
static void run();
static void select_desktop(const Arg arg);
static void send_kill_signal(Window w);
static void setup();
static void sigchld();
static void spawn(const Arg arg);
static void start(const Arg arg);
static void swap_master();
static void switch_mode(const Arg arg);
static void tile();
static client* get_current();
static client* get_head();
static void update_current();
static void update_current_desktop();
static void update_title();
static void focus();
static void unfocus();

static int xerror(Display *dis, XErrorEvent *ee);
static void (*events[LASTEvent]) (XEvent *e) = {
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [MapRequest] = maprequest,
    [DestroyNotify] = destroynotify,
    [KeyPress] = keypress,
};

// Global variables
static Display *dis;
static int bool_quit;
static int randrbase;
static int screen;
static unsigned int current_desktop;
static int master_size;
static int mode;
static Window root;
static client *head;
static client *current;
static desktop desktops[10];

static void add_window(Window w) {
    client *c, *t = head;

    if(!(c = (client *)calloc(1,sizeof(client))))
        die("Error calloc!");

    if(head == NULL) {
        c->next = NULL;
        c->prev = NULL;
        c->win = w;
        head = c;
    }
    else {
        for(t=head;t->next;t=t->next);

        c->next = NULL;
        c->prev = t;
        c->win = w;
        t->next = c;
    }
    current = c;
}

void change_desktop(const Arg arg) {
    client *c;

    if(arg.i == current_desktop)
        return;

    // Unmap all window
    if(head != NULL)
        for(c=head;c;c=c->next)
            XUnmapWindow(dis,c->win);

    // Save current "properties"
    desktops[current_desktop].head = head;
    desktops[current_desktop].current = current;
    desktops[current_desktop].master_size = master_size;
    desktops[current_desktop].mode = mode;

    // Change desktop
    current_desktop = arg.i;
    head = desktops[current_desktop].head;
    current = desktops[current_desktop].current;
    master_size = desktops[current_desktop].master_size;
    mode = desktops[current_desktop].mode;

    // Map all windows
    if(head != NULL)
        for(c=head;c;c=c->next)
            XMapWindow(dis,c->win);

    tile();
}

// Send current window to specified desktop
void client_to_desktop(const Arg arg) {
    client *tmp = current;

    if(arg.i == current_desktop || !current)
        return;

    // Add client to desktop
    desktops[arg.i].master_size = master_size;
    desktops[arg.i].mode = mode;
    if(!(desktops[arg.i].head)) {
        desktops[arg.i].head = tmp;
        desktops[arg.i].current = tmp;
        current = desktops[current_desktop].head = tmp->next;
    }
    else {
        current = desktops[arg.i].current;
        desktops[arg.i].current = tmp;
        current = desktops[current_desktop].head;
        for(;current->next;current=current->next);
        current->next = tmp;
        tmp->prev = current;
    }

    // Remove client from current desktop
    if(tmp->prev)
        tmp->prev->next = tmp->next;
    if(tmp->next)
        tmp->next->prev = tmp->prev;
    if(tmp == head)
        head = tmp->next;
    if(tmp == current)
        current = head;

    // Prepare client for new desktop
    tmp->next = NULL;
    tmp->prev = NULL;
    current = desktops[current_desktop].current;
    tile();
}

void configurenotify(XEvent *e) {
    XConfigureEvent *ev = &e->xconfigure;

    if(ev->window == root) {
        screen = DefaultScreen(dis);
        tile();
    }
}

void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dis, ev->window, ev->value_mask, &wc);
}

void decrease() {
    if(master_size > 50) {
        master_size -= 10;
        tile();
    }
}

void destroynotify(XEvent *e) {
    int i=0;
    client *c;

    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if(head == NULL)
        return;

    if(current && ev->window == current->win) {
        if(current->prev)
            current = current->prev;
        else if(current->next)
            current = current->next;
        else
            current = NULL;
    }

    for(c=head;c;c=c->next) {
        if(c->win == ev->window) {
            if(c->prev)
                c->prev->next = c->next;
            if(c->next)
                c->next->prev = c->prev;
            if(c == head)
                head = c->next;
            free(c);
            break;
        }
        i++;
    }

    if(!head)
        current = NULL;

    tile();
}

void die(const char* e) {
    /* patched: avoid stdout symbol issues on NetBSD */
    printf("tigerwm: %s\n",e);
    exit(1);
}

unsigned long getcolor(const char* color) {
    XColor c;
    Colormap map = DefaultColormap(dis,screen);

    if(!XAllocNamedColor(dis,map,color,&c,&c))
        die("Error parsing color!");

    return c.pixel;
}

void grabkeys() {
    updatetitle:
    XUngrabKey(dis, AnyKey, AnyModifier, root);
    KeyCode code;
    unsigned int i;

    for(i=0;i < sizeof(keys)/sizeof(*keys);i++) {
        code = XKeysymToKeycode(dis,keys[i].keysym);
        XGrabKey(dis,code,keys[i].mod,root,True,GrabModeAsync,GrabModeAsync);
    }
}

void increase() {
    if(master_size < 80) {
        master_size += 10;
        tile();
    }
}

void keypress(XEvent *e) {
    unsigned int i;
    KeySym keysym;
    XKeyEvent ke = e->xkey;

    keysym = XKeycodeToKeysym(dis,(KeyCode)ke.keycode,0);
    for(i=0;i < sizeof(keys)/sizeof(*keys);i++) {
        if(keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ke.state)) {
            if(keys[i].function)
                keys[i].function(keys[i].arg);
        }
    }
}

void kill_client() {
    if(!current)
        return;
    send_kill_signal(current->win);
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;

    if(!XGetWindowAttributes(dis,ev->window,&wa) || wa.override_redirect)
        return;

    add_window(ev->window);
    XSelectInput(dis,ev->window,PropertyChangeMask | StructureNotifyMask);

    tile();
}

void move_down() {
    client *c;

    if(!current || !head->next)
        return;

    c = current;
    if(c->next) {
        if(c->prev) {
            c->prev->next = c->next;
            c->next->prev = c->prev;
        } else {
            head = c->next;
            c->next->prev = NULL;
        }

        if(c->next->next) {
            c->next->next->prev = c;
            c->prev = c->next;
            c->next = c->next->next;
            c->prev->next = c;
        } else {
            c->next->next = c;
            c->prev = c->next;
            c->next = NULL;
        }
    }

    tile();
}

void move_up() {
    client *c;

    if(!current || !head->next)
        return;

    c = current;
    if(c->prev) {
        if(c->next) {
            c->prev->next = c->next;
            c->next->prev = c->prev;
        } else {
            c->prev->next = NULL;
        }

        if(c->prev->prev) {
            c->prev->prev->next = c;
            c->next = c->prev;
            c->prev = c->prev->prev;
            c->next->prev = c;
        } else {
            c->next = head;
            c->prev = NULL;
            head->prev = c;
            head = c;
        }
    }

    tile();
}

void next_desktop() {
    Arg arg;
    arg.i = (current_desktop + 1) % DESKTOPS;
    change_desktop(arg);
}

void next_win() {
    if(!current || !current->next)
        current = head;
    else
        current = current->next;

    tile();
}

void prev_desktop() {
    Arg arg;
    arg.i = (current_desktop + DESKTOPS - 1) % DESKTOPS;
    change_desktop(arg);
}

void prev_win() {
    if(!current || !current->prev) {
        for(current=head;current->next;current=current->next);
    } else {
        current = current->prev;
    }

    tile();
}

void quit() {
    Window root_return, parent;
    Window *children;
    int i;
    unsigned int nchildren;
    XEvent ev;

    /*
     * if a client refuses to terminate itself,
     * we kill every window remaining the brutal way.
     * Since we're stuck in the while(nchildren > 0) { ... } loop
     * we can't exit through the main method.
     * This all happens if MOD+q is pushed a second time.
     */
    if(bool_quit == 1) {
        XUngrabKey(dis, AnyKey, AnyModifier, root);
        XDestroySubwindows(dis, root);
        /* patched: remove stdout usage */
        printf("tigerwm: Thanks for using!\n");
        XCloseDisplay(dis);
        die("forced shutdown");
    }

    bool_quit = 1;
    XQueryTree(dis, root, &root_return, &parent, &children, &nchildren);
    for(i = 0; i < nchildren; i++) {
        send_kill_signal(children[i]);
    }
    //keep alive until all windows are killed
    while(nchildren > 0) {
        XQueryTree(dis, root, &root_return, &parent, &children, &nchildren);
        XNextEvent(dis,&ev);
        if(events[ev.type])
            events[ev.type](&ev);
    }

    XUngrabKey(dis,AnyKey,AnyModifier,root);
    /* patched */
    printf("tigerwm: Thanks for using!\n");
}

void remove_window(Window w) {
    client *c;

    if(head == NULL)
        return;

    if(current && w == current->win) {
        if(current->prev)
            current = current->prev;
        else if(current->next)
            current = current->next;
        else
            current = NULL;
    }

    for(c=head;c;c=c->next) {
        if(c->win == w) {
            if(c->prev)
                c->prev->next = c->next;
            if(c->next)
                c->next->prev = c->prev;
            if(c == head)
                head = c->next;
            free(c);
            break;
        }
    }

    if(!head)
        current = NULL;

    tile();
}

void resizemaster(const Arg arg) {
    if(arg.i == 0)
        return;

    master_size += arg.i;
    if(master_size < 10)
        master_size = 10;
    if(master_size > 90)
        master_size = 90;

    tile();
}

void run() {
    XEvent ev;

    while(1) {
        XNextEvent(dis,&ev);
        if(events[ev.type])
            events[ev.type](&ev);
    }
}

void select_desktop(const Arg arg) {
    change_desktop(arg);
}

void send_kill_signal(Window w) {
    int n, format;
    Atom *protocols, wm_delete;
    Atom type;
    XEvent ev;

    wm_delete = XInternAtom(dis,"WM_DELETE_WINDOW",True);
    if(wm_delete == None)
        return;

    if(XGetWMProtocols(dis,w,&protocols,&n)) {
        while(n--) {
            if(protocols[n] == wm_delete) {
                ev.type = ClientMessage;
                ev.xclient.window = w;
                ev.xclient.message_type = XInternAtom(dis,"WM_PROTOCOLS",True);
                ev.xclient.format = 32;
                ev.xclient.data.l[0] = wm_delete;
                ev.xclient.data.l[1] = CurrentTime;
                XSendEvent(dis,w,False,NoEventMask,&ev);
                XFree(protocols);
                return;
            }
        }
        XFree(protocols);
    }

    XDestroyWindow(dis,w);
}

void setup() {
    unsigned int i;
    XSetWindowAttributes attr;

    if(!(dis = XOpenDisplay(NULL)))
        die("Cannot open display!");

    screen = DefaultScreen(dis);
    root = RootWindow(dis,screen);

    attr.border_pixel = getcolor(BORDER_COLOR);
    attr.background_pixel = getcolor(FOCUS_COLOR);
    attr.colormap = DefaultColormap(dis,screen);
    attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | EnterWindowMask | LeaveWindowMask | StructureNotifyMask;
    XChangeWindowAttributes(dis,root,CWBorderPixel | CWBackPixel | CWColormap | CWEventMask,&attr);

    XSelectInput(dis,root,SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | EnterWindowMask | LeaveWindowMask | StructureNotifyMask);

    // Init desktops
    for(i=0;i<DESKTOPS;i++) {
        desktops[i].master_size = master_size;
        desktops[i].mode = mode;
        desktops[i].head = NULL;
        desktops[i].current = NULL;
    }

    grabkeys();
}

void sigchld() {
    while(0 < waitpid(-1,NULL,WNOHANG));
}

void spawn(const Arg arg) {
    if(fork() == 0) {
        if(fork() == 0) {
            setsid();
            execvp((char*)arg.com[0],(char**)arg.com);
        }
        exit(0);
    }
}

void start(const Arg arg) {
    spawn(arg);
}

void swap_master() {
    client *c;

    if(!head || !current || head == current)
        return;

    c = current;

    if(c->prev)
        c->prev->next = c->next;
    if(c->next)
        c->next->prev = c->prev;

    c->next = head;
    c->prev = NULL;
    head->prev = c;
    head = c;

    tile();
}

void switch_mode(const Arg arg) {
    desktops[current_desktop].mode = arg.i;
    mode = arg.i;
    tile();
}

void tile() {
    int master_x, master_y, master_width, master_height;
    int stack_x, stack_y, stack_width, stack_height;
    client *c;
    int n = 0;

    for(c=head;c;c=c->next)
        n++;

    if(n == 0)
        return;

    master_x = 0;
    master_y = 0;
    master_width = (mode == 0 ? (800 * master_size / 100) : 800);
    master_height = (mode == 1 ? (600 * master_size / 100) : 600);

    stack_x = master_width;
    stack_y = 0;
    stack_width = 800 - master_width;
    stack_height = 600 / (n - 1);

    for(c=head;c;c=c->next) {
        if(c == head) {
            XMoveResizeWindow(dis,c->win,master_x,master_y,master_width,master_height);
        } else {
            XMoveResizeWindow(dis,c->win,stack_x,stack_y,stack_width,stack_height);
            stack_y += stack_height;
        }
    }
}

static int xerror(Display *dis, XErrorEvent *ee) {
    switch(ee->error_code) {
        case BadAccess:
        case BadAlloc:
        case BadAtom:
        case BadColor:
        case BadCursor:
        case BadDrawable:
        case BadFont:
        case BadGC:
        case BadIDChoice:
        case BadImplementation:
        case BadLength:
        case BadMatch:
        case BadName:
        case BadPixmap:
        case BadRequest:
        case BadValue:
        case BadWindow:
            break;
    }
    return 0;
}

int main() {
    XEvent ev;

    setup();
    XSync(dis, False);

    while(1) {
        XNextEvent(dis,&ev);
        if(events[ev.type])
            events[ev.type](&ev);
    }

    return 0;
}
