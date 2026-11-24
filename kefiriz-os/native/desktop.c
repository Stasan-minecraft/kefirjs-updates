#define _POSIX_C_SOURCE 200809L
#include "desktop_input.h"
#include "draw.h"
#include "libretro.h"
#include "version.h"
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/keysym.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct input_sample {
    unsigned buttons;
    int16_t x, y;
    bool touching;
};
struct viewport {
    int x, y, width, height;
};
enum { QUEUE_SIZE = 64 };
static Display *display;
static Window root, window;
static Atom wm_protocols, wm_close;
static Pixmap frame_pixmap;
static Picture src, dst;
static GC gc;
static XImage *image;
static struct viewport view;
static uint64_t uploaded;
static bool redraw, active, mouse_held, failed;
static bool presented;
static volatile sig_atomic_t stop;
static unsigned keys[256], pad_mask, keyboard_mask;
static struct input_sample live, sample, queue[QUEUE_SIZE];
static unsigned head, count;

static void enqueue(void) {
    live.buttons = active ? keyboard_mask | pad_mask : 0;
    const struct input_sample *prior = count ? &queue[(head + count - 1) % QUEUE_SIZE] : &sample;
    if (live.buttons == prior->buttons && live.touching == prior->touching &&
        (!live.touching || (live.x == prior->x && live.y == prior->y)))
        return;
    if (count >= 2) {
        const struct input_sample *older = &queue[(head + count - 2) % QUEUE_SIZE];
        // Coalesce drag motion, keeping the original press point and button transitions.
        if (older->touching && prior->touching && live.touching &&
            older->buttons == prior->buttons && prior->buttons == live.buttons) {
            queue[(head + count - 1) % QUEUE_SIZE] = live;
            return;
        }
    }
    if (count == QUEUE_SIZE) {
        // Resynchronize to the live device instead of replaying stale events after a burst.
        head = count = 0;
    }
    queue[(head + count++) % QUEUE_SIZE] = live;
}
static void pad_changed(unsigned bits) {
    pad_mask = bits;
    enqueue();
}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port || index)
        return 0;
    if (device == RETRO_DEVICE_JOYPAD && id < 16)
        return (sample.buttons >> id) & 1;
    if (device == RETRO_DEVICE_POINTER) {
        if (id == RETRO_DEVICE_ID_POINTER_X)
            return sample.x;
        if (id == RETRO_DEVICE_ID_POINTER_Y)
            return sample.y;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED)
            return sample.touching;
    }
    return 0;
}
static void poll_input(void) {
    if (count) {
        sample = queue[head];
        head = (head + 1) % QUEUE_SIZE;
        count--;
    }
}
static unsigned key_mask(KeySym sym) {
#define KEY_BIT(id) (1u << RETRO_DEVICE_ID_JOYPAD_##id)
    switch (sym) {
    case XK_Up:
        return KEY_BIT(UP);
    case XK_Down:
        return KEY_BIT(DOWN);
    case XK_Left:
        return KEY_BIT(LEFT);
    case XK_Right:
        return KEY_BIT(RIGHT);
    case XK_a:
    case XK_Return:
    case XK_KP_Enter:
        return KEY_BIT(A);
    case XK_b:
    case XK_Escape:
    case XK_BackSpace:
        return KEY_BIT(B);
    case XK_x:
        return KEY_BIT(X);
    case XK_y:
        return KEY_BIT(Y);
    case XK_Tab:
    case XK_equal:
    case XK_KP_Add:
        return KEY_BIT(START);
    case XK_minus:
        return KEY_BIT(SELECT);
    case XK_l:
        return KEY_BIT(L);
    case XK_r:
        return KEY_BIT(R);
    case XK_t:
        return KEY_BIT(R2);
    default:
        return 0;
    }
#undef KEY_BIT
}
static void key(XKeyEvent *ev, bool down) {
    if (ev->keycode >= 256)
        return;
    keys[ev->keycode] = down ? key_mask(XLookupKeysym(ev, 0)) : 0;
    keyboard_mask = 0;
    for (size_t i = 0; i < 256; i++)
        keyboard_mask |= keys[i];
    enqueue();
}
static bool pointer_position(int x, int y) {
    x -= view.x;
    y -= view.y;
    if (x < 0 || x >= view.width || y < 0 || y >= view.height) {
        live.x = live.y = INT16_MIN;
        return false;
    }
    live.x = (int16_t)((2LL * x + 1) * 32768 / view.width - 32768);
    live.y = (int16_t)((2LL * y + 1) * 32768 / view.height - 32768);
    return true;
}
static void resize(int width, int height) {
    int w = width, h = height;
    if (width <= 0 || height <= 0)
        return;
    if ((int64_t)width * KZ_HEIGHT > (int64_t)height * KZ_WIDTH)
        w = (int)((int64_t)height * KZ_WIDTH / KZ_HEIGHT);
    else
        h = (int)((int64_t)width * KZ_HEIGHT / KZ_WIDTH);
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    view = (struct viewport){(width - w) / 2, (height - h) / 2, w, h};
    XTransform scale = {{{XDoubleToFixed((double)KZ_WIDTH / w), 0, 0},
                         {0, XDoubleToFixed((double)KZ_HEIGHT / h), 0},
                         {0, 0, XDoubleToFixed(1.0)}}};
    XRenderSetPictureTransform(display, src, &scale);
    XClearWindow(display, window);
    redraw = true;
}
static void event(XEvent *ev) {
    switch (ev->type) {
    case MapNotify:
        if (ev->xmap.window == window)
            XSetInputFocus(display, window, RevertToParent, CurrentTime);
        break;
    case FocusIn:
        active = true;
        enqueue();
        break;
    case FocusOut:
        active = false;
        memset(keys, 0, sizeof keys);
        keyboard_mask = 0;
        mouse_held = live.touching = false;
        head = count = 0;
        sample = (struct input_sample){0};
        enqueue();
        break;
    case Expose:
        redraw = true;
        break;
    case ConfigureNotify:
        if (ev->xconfigure.window == root)
            XMoveResizeWindow(display, window, 0, 0, (unsigned)ev->xconfigure.width,
                              (unsigned)ev->xconfigure.height);
        if (ev->xconfigure.window == window)
            resize(ev->xconfigure.width, ev->xconfigure.height);
        break;
    case KeyPress:
    case KeyRelease:
        key(&ev->xkey, ev->type == KeyPress);
        break;
    case ButtonPress:
        if (ev->xbutton.button == Button1 && active) {
            mouse_held = live.touching = pointer_position(ev->xbutton.x, ev->xbutton.y);
            enqueue();
        }
        break;
    case MotionNotify:
        if (mouse_held) {
            pointer_position(ev->xmotion.x, ev->xmotion.y);
            enqueue();
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button == Button1 && mouse_held) {
            pointer_position(ev->xbutton.x, ev->xbutton.y);
            enqueue();
            mouse_held = live.touching = false;
            enqueue();
        }
        break;
    case ClientMessage:
        if (ev->xclient.message_type == wm_protocols && (Atom)ev->xclient.data.l[0] == wm_close)
            stop = 1;
        break;
    case DestroyNotify:
        if (ev->xdestroywindow.window == window)
            stop = 1;
        break;
    }
}
static bool environment(unsigned request, void *data) {
    if (request == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    return request == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME;
}
static const char *switch_board(const char *data, size_t length) {
    // Root compatible strings in the pinned Switchroot nx-plat.dtimg.
    static const char *const boards[] = {
        "nintendo,icosa",
        "nintendo,iowa",
        "nintendo,hoag",
        "nintendo,aula",
    };
    while (length) {
        const char *end = memchr(data, 0, length);
        if (!end)
            return NULL;
        for (size_t i = 0; i < sizeof boards / sizeof boards[0]; i++)
            if (!strcmp(data, boards[i]))
                return boards[i];
        size_t count = (size_t)(end - data) + 1;
        data += count;
        length -= count;
    }
    return NULL;
}
static void record_session(void) {
    char compatible[512];
    int fd = open("/proc/device-tree/compatible", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    ssize_t n = read(fd, compatible, sizeof compatible);
    close(fd);
    if (n <= 0 || n == (ssize_t)sizeof compatible)
        return;
    const char *board = switch_board(compatible, (size_t)n);
    if (!board)
        return;
    const char final[] = "/storage/kefiriz/session-" KF_OS_VERSION ".txt";
    char temp[] = "/storage/kefiriz/.session-XXXXXX";
    char report[96];
    int length = snprintf(report, sizeof report, "version=%s\nboard=%s\n", KF_OS_VERSION, board);
    if (length < 0 || (size_t)length >= sizeof report)
        return;
    fd = open(final, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd >= 0) {
        char saved[sizeof report];
        ssize_t size = read(fd, saved, sizeof saved);
        close(fd);
        if (size == length && !memcmp(saved, report, (size_t)length))
            return;
    }
    fd = mkstemp(temp);
    if (fd < 0) {
        perror("Cannot create desktop session marker");
        return;
    }
    bool written = write(fd, report, (size_t)length) == length && fsync(fd) == 0;
    if (close(fd) != 0)
        written = false;
    if (!written || rename(temp, final) != 0) {
        fputs("Could not save desktop session marker\n", stderr);
        unlink(temp);
        return;
    }
    fd = open("/storage/kefiriz", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
static void video(const void *pixels, unsigned w, unsigned h, size_t pitch) {
    if (!pixels || w != KZ_WIDTH || h != KZ_HEIGHT || pitch != (size_t)image->bytes_per_line) {
        fputs("Unexpected KEFIRIZ framebuffer format\n", stderr);
        failed = true;
        stop = 1;
        return;
    }
    uint64_t generation = kz_frame_generation();
    if (uploaded != generation) {
        image->data = (char *)pixels;
        XPutImage(display, frame_pixmap, gc, image, 0, 0, 0, 0, w, h);
        image->data = NULL;
        uploaded = generation;
        redraw = true;
    }
    if (redraw) {
        XRenderComposite(display, PictOpSrc, src, None, dst, 0, 0, 0, 0, view.x, view.y,
                         (unsigned)view.width, (unsigned)view.height);
        if (!presented) {
            XSync(display, False);
            presented = true;
            record_session();
        } else
            XFlush(display);
        redraw = false;
    }
}
static bool open_display(void) {
    display = XOpenDisplay(NULL);
    if (!display) {
        fputs("Cannot open the KEFIRIZ X11 display\n", stderr);
        return false;
    }
    int s = DefaultScreen(display);
    root = RootWindow(display, s);
    Visual *visual = DefaultVisual(display, s);
    unsigned depth = (unsigned)DefaultDepth(display, s);
    int extension_event, extension_error;
    if (!XRenderQueryExtension(display, &extension_event, &extension_error)) {
        fputs("The X11 display does not support Render\n", stderr);
        return false;
    }
    XRenderPictFormat *format = XRenderFindVisualFormat(display, visual);
    image = XCreateImage(display, visual, depth, ZPixmap, 0, NULL, KZ_WIDTH, KZ_HEIGHT, 32, 0);
    if (!format || !image || image->byte_order != LSBFirst || image->bits_per_pixel != 32 ||
        visual->red_mask != 0xff0000 || visual->green_mask != 0xff00 || visual->blue_mask != 0xff) {
        fputs("The KEFIRIZ display requires an RGB888 TrueColor visual\n", stderr);
        return false;
    }
    XSetWindowAttributes attrs = {
        .background_pixel = BlackPixel(display, s),
        .override_redirect = True,
        .event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | ExposureMask | StructureNotifyMask | FocusChangeMask};
    unsigned width = (unsigned)DisplayWidth(display, s),
             height = (unsigned)DisplayHeight(display, s);
    window = XCreateWindow(display, root, 0, 0, width, height, 0, (int)depth, InputOutput, visual,
                           CWBackPixel | CWEventMask | CWOverrideRedirect, &attrs);
    XStoreName(display, window, "KEFIRIZ OS");
    XClassHint hint = {.res_name = "kefiriz-desktop", .res_class = "KefirizDesktop"};
    XSetClassHint(display, window, &hint);
    wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    wm_close = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_close, 1);
    Bool supported = False;
    XkbSetDetectableAutoRepeat(display, True, &supported);
    XSelectInput(display, root, StructureNotifyMask);
    static const char empty[1] = {0};
    Pixmap blank = XCreateBitmapFromData(display, window, empty, 1, 1);
    XColor color = {0};
    Cursor cursor = XCreatePixmapCursor(display, blank, blank, &color, &color, 0, 0);
    XDefineCursor(display, window, cursor);
    XFreeCursor(display, cursor);
    XFreePixmap(display, blank);
    frame_pixmap = XCreatePixmap(display, window, KZ_WIDTH, KZ_HEIGHT, depth);
    gc = XCreateGC(display, frame_pixmap, 0, NULL);
    src = XRenderCreatePicture(display, frame_pixmap, format, 0, NULL);
    dst = XRenderCreatePicture(display, window, format, 0, NULL);
    XRenderSetPictureFilter(display, src, FilterBilinear, NULL, 0);
    resize((int)width, (int)height);
    XMapRaised(display, window);
    XSync(display, False);
    return true;
}
static void terminate(int number) {
    (void)number;
    stop = 1;
}
static int64_t monotonic(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
int main(void) {
    if (!open_display()) {
        if (image)
            XDestroyImage(image);
        if (display)
            XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    struct sigaction action = {.sa_handler = terminate};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    kz_pads_init();
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_input_poll(poll_input);
    retro_set_input_state(input_state);
    retro_init();
    if (!retro_load_game(NULL)) {
        fputs("Cannot initialize KEFIRIZ desktop\n", stderr);
        failed = true;
    }
    int64_t next = monotonic();
    while (!stop && !failed) {
        while (XPending(display)) {
            XEvent ev;
            XNextEvent(display, &ev);
            event(&ev);
        }
        kz_pads_poll(pad_changed);
        if (stop)
            break;
        retro_run();
        next += 1000000000LL / 30;
        int64_t now = monotonic();
        if (now > next)
            next = now;
        struct timespec deadline = {.tv_sec = (time_t)(next / 1000000000),
                                    .tv_nsec = (long)(next % 1000000000)};
        while (!stop && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) == EINTR) {
        }
    }
    retro_unload_game();
    retro_deinit();
    kz_pads_close();
    XDestroyImage(image);
    XCloseDisplay(display);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
