/*
 * swp — simple wallpaper (X11)
 *
 * Minimal wallpaper setter using Xlib + Imlib2.
 *
 * - Aspect-aware scaling:
 *     default (cover): fill screen, crop center to preserve aspect
 *     -f (fit):       letterbox, center without cropping
 *     -S (stretch):   stretch to screen (may distort)
 *
 * Usage:
 *   swp [-f | -S] /path/to/image
 *
 * Build:
 *   cc -O2 -pipe -Wall -Wextra -std=c11 swp.c -o swp \
 *      `pkg-config --cflags --libs x11 imlib2`
 *   # OpenBSD: add -Wl,-z,relro -Wl,-z,now if desired
 */

#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <Imlib2.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __OpenBSD__
#include <err.h>     /* concise failure paths (OpenBSD). */
#include <unistd.h>  /* pledge, unveil */
#endif

enum mode { MODE_COVER = 0, MODE_FIT, MODE_STRETCH };

static void
usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [-f | -S] image\n", argv0);
    fprintf(stderr, "  default: cover (fill, crop center)\n");
    fprintf(stderr, "  -f: fit (letterbox)\n");
    fprintf(stderr, "  -S: stretch (may distort)\n");
}

static int
get_root_size(Display *dpy, int screen, int *sw, int *sh)
{
    if (!dpy || !sw || !sh) return -1;
    *sw = DisplayWidth(dpy, screen);
    *sh = DisplayHeight(dpy, screen);
    if (*sw <= 0 || *sh <= 0) return -1;
    return 0;
}

static void
publish_root_pixmap(Display *dpy, Window root, Pixmap pm)
{
    /* Publish IDs so others can clean up and background survives after exit. */
    Atom a_xroot = XInternAtom(dpy, "_XROOTPMAP_ID", False);
    Atom a_eset  = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

    /* Read previous pixmap to kill its owner, preventing leaks. */
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    Pixmap old = 0;

    if (XGetWindowProperty(dpy, root, a_xroot, 0, 1, False, XA_PIXMAP,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data)
        == Success && actual_type == XA_PIXMAP && actual_format == 32 && nitems == 1) {
        old = *(Pixmap *)data;
    }
    if (data) XFree(data);

    /* Set both properties to the new pixmap. */
    unsigned long p = (unsigned long)pm; /* XIDs are 32-bit; format must be 32. */
    XChangeProperty(dpy, root, a_xroot, XA_PIXMAP, 32, PropModeReplace,
                    (unsigned char *)&p, 1);
    XChangeProperty(dpy, root, a_eset,  XA_PIXMAP, 32, PropModeReplace,
                    (unsigned char *)&p, 1);

    XSetWindowBackgroundPixmap(dpy, root, pm);
    XClearWindow(dpy, root);
    XFlush(dpy);

    /* Kill old pixmap owner after we successfully set the background. */
    if (old && old != pm) {
        XKillClient(dpy, old);
    }

    /* Crucial: keep our pixmap after exit. */
    XSetCloseDownMode(dpy, RetainPermanent);
}

int
main(int argc, char **argv)
{
    const char *argv0 = argv[0];
    enum mode m = MODE_COVER;

    int opt;
    while ((opt = getopt(argc, argv, "fSh")) != -1) {
        switch (opt) {
        case 'f': m = MODE_FIT; break;
        case 'S': m = MODE_STRETCH; break;
        case 'h': usage(argv0); return 0;
        default:  usage(argv0); return 1;
        }
    }

    if (optind != argc - 1) {
        usage(argv0);
        return 1;
    }
    const char *imgpath = argv[optind];

#ifdef __OpenBSD__
    /* Constrain FS view to the single image; allow X11 unix socket. */
    if (unveil(imgpath, "r") == -1) err(1, "unveil(%s)", imgpath);
    if (unveil(NULL, NULL) == -1) err(1, "unveil lock");
    if (pledge("stdio rpath unix", NULL) == -1) err(1, "pledge");
#endif

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "swp: cannot open display\n");
        return 1;
    }
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    /* Load image (Imlib2 does formats securely and fast). */
    Imlib_Image img = imlib_load_image(imgpath);
    if (!img) {
        fprintf(stderr, "swp: failed to load %s\n", imgpath);
        XCloseDisplay(dpy);
        return 1;
    }
    imlib_context_set_image(img);
    int iw = imlib_image_get_width();
    int ih = imlib_image_get_height();
    if (iw <= 0 || ih <= 0) {
        fprintf(stderr, "swp: bad image dimensions\n");
        imlib_free_image();
        XCloseDisplay(dpy);
        return 1;
    }

#ifdef __OpenBSD__
    /* Drop 'rpath' once file is loaded. */
    if (pledge("stdio unix", NULL) == -1) err(1, "pledge (tighten)");
#endif

    int sw, sh;
    if (get_root_size(dpy, screen, &sw, &sh) != 0) {
        fprintf(stderr, "swp: cannot query screen size\n");
        imlib_free_image();
        XCloseDisplay(dpy);
        return 1;
    }

    /* Prepare scaled/cropped image per mode. */
    imlib_context_set_anti_alias(1);

    Imlib_Image work = NULL;
    int dx = 0, dy = 0;  /* dest offset for FIT */

    if (m == MODE_STRETCH) {
        work = imlib_create_cropped_scaled_image(0, 0, iw, ih, sw, sh);
        if (!work) {
            fprintf(stderr, "swp: scaling failed (stretch)\n");
            imlib_free_image();
            XCloseDisplay(dpy);
            return 1;
        }
    } else if (m == MODE_COVER) {
        /* Crop source to screen aspect, then scale to fill. */
        double src_aspect = (double)iw / (double)ih;
        double dst_aspect = (double)sw / (double)sh;
        int sx = 0, sy = 0, cw = iw, ch = ih;

        if (src_aspect > dst_aspect) {
            /* Wider than target: crop width. */
            cw = (int)llround((double)ih * dst_aspect);
            sx = (iw - cw) / 2;
        } else if (src_aspect < dst_aspect) {
            /* Taller than target: crop height. */
            ch = (int)llround((double)iw / dst_aspect);
            sy = (ih - ch) / 2;
        }
        work = imlib_create_cropped_scaled_image(sx, sy, cw, ch, sw, sh);
        if (!work) {
            fprintf(stderr, "swp: scaling failed (cover)\n");
            imlib_free_image();
            XCloseDisplay(dpy);
            return 1;
        }
    } else { /* MODE_FIT */
        /* Scale to fit inside screen, center; preserve aspect. */
        double scale_w = (double)sw / (double)iw;
        double scale_h = (double)sh / (double)ih;
        double scale = (scale_w < scale_h) ? scale_w : scale_h;
        int nw = (int)llround((double)iw * scale);
        int nh = (int)llround((double)ih * scale);
        if (nw <= 0) nw = 1;
        if (nh <= 0) nh = 1;

        work = imlib_create_cropped_scaled_image(0, 0, iw, ih, nw, nh);
        if (!work) {
            fprintf(stderr, "swp: scaling failed (fit)\n");
            imlib_free_image();
            XCloseDisplay(dpy);
            return 1;
        }
        dx = (sw - nw) / 2;
        dy = (sh - nh) / 2;
    }

    /* Ready to render to a pixmap; set full X/imlib context. */
    imlib_free_image();               /* free original */
    imlib_context_set_image(work);

    Visual   *vis = DefaultVisual(dpy, screen);
    Colormap  cmap = DefaultColormap(dpy, screen);
    int       depth = DefaultDepth(dpy, screen);

    Pixmap pm = XCreatePixmap(dpy, root, (unsigned)sw, (unsigned)sh, (unsigned)depth);
    if (!pm) {
        fprintf(stderr, "swp: XCreatePixmap failed\n");
        imlib_free_image();
        XCloseDisplay(dpy);
        return 1;
    }

    imlib_context_set_display(dpy);
    imlib_context_set_visual(vis);
    imlib_context_set_colormap(cmap);
    imlib_context_set_drawable(pm);

    if (m == MODE_FIT) {
        /* Clear background behind centered image. */
        GC gc = XCreateGC(dpy, pm, 0, NULL);
        if (!gc) {
            fprintf(stderr, "swp: XCreateGC failed\n");
            imlib_free_image();
            /* We must free pm here since we haven't published it. */
            XFreePixmap(dpy, pm);
            XCloseDisplay(dpy);
            return 1;
        }
        XSetForeground(dpy, gc, BlackPixel(dpy, screen)); /* keep simple & readable */
        XFillRectangle(dpy, pm, gc, 0, 0, (unsigned)sw, (unsigned)sh);
        XFreeGC(dpy, gc);

        imlib_render_image_on_drawable(dx, dy);
    } else {
        imlib_render_image_on_drawable(0, 0);
    }

    /* Publish as the root background (and keep it alive after exit). */
    publish_root_pixmap(dpy, root, pm);

    /* We DO NOT free 'pm'; server retains it due to RetainPermanent. */
    imlib_free_image();

    XCloseDisplay(dpy);
    return 0;
}
