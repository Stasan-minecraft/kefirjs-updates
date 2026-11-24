#define _POSIX_C_SOURCE 200809L
#include "desktop_input.h"
#include "libretro.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PAD_BIT(id) (1u << RETRO_DEVICE_ID_JOYPAD_##id)
#define KEY_BITS ((KEY_MAX + 8) / 8)
enum { MAX_GAMEPADS = 16 };
static const char device_dir[] = "/dev/input";

struct axis {
    int code;
    int min, max;
    unsigned low, high, buttons;
};
struct gamepad {
    int fd;
    char name[32];
    unsigned keys, reported;
    int dropped;
    struct axis axes[6];
};
static struct gamepad pads[MAX_GAMEPADS];
static int watch_fd = -1;
static unsigned last_mask, scan_count;

static const struct {
    int code;
    unsigned bit;
} buttons[] = {{BTN_SOUTH, PAD_BIT(B)},        {BTN_EAST, PAD_BIT(A)},
               {BTN_NORTH, PAD_BIT(X)},        {BTN_WEST, PAD_BIT(Y)},
               {BTN_DPAD_UP, PAD_BIT(UP)},     {BTN_DPAD_DOWN, PAD_BIT(DOWN)},
               {BTN_DPAD_LEFT, PAD_BIT(LEFT)}, {BTN_DPAD_RIGHT, PAD_BIT(RIGHT)},
               {BTN_START, PAD_BIT(START)},    {BTN_SELECT, PAD_BIT(SELECT)},
               {BTN_TL, PAD_BIT(L)},           {BTN_TR, PAD_BIT(R)},
               {BTN_TL2, PAD_BIT(L2)},         {BTN_TR2, PAD_BIT(R2)},
               {BTN_THUMBL, PAD_BIT(L3)},      {BTN_THUMBR, PAD_BIT(R3)}};

static int bit_set(const uint8_t *bits, int code) { return bits[code / 8] & (1u << (code % 8)); }
static void read_axis(struct axis *axis, int value) {
    if (axis->max <= axis->min)
        return;
    int64_t span = (int64_t)axis->max - axis->min;
    int64_t offset = (int64_t)value - axis->min;
    axis->buttons = offset * 4 < span ? axis->low : offset * 4 > span * 3 ? axis->high : 0;
}
static void resync(struct gamepad *pad) {
    uint8_t keys[KEY_BITS] = {0};
    pad->keys = 0;
    if (ioctl(pad->fd, EVIOCGKEY(sizeof keys), keys) >= 0)
        for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
            if (bit_set(keys, buttons[i].code))
                pad->keys |= buttons[i].bit;
    for (size_t i = 0; i < sizeof pad->axes / sizeof pad->axes[0]; i++) {
        struct input_absinfo info;
        struct axis *a = &pad->axes[i];
        a->min = a->max = 0;
        a->buttons = 0;
        if (ioctl(pad->fd, EVIOCGABS(a->code), &info) < 0)
            continue;
        a->min = info.minimum;
        a->max = info.maximum;
        read_axis(a, info.value);
    }
}
static void report(struct gamepad *pad, void (*notify)(unsigned)) {
    unsigned mask = 0;
    if (pad->fd >= 0) {
        mask = pad->keys;
        for (size_t i = 0; i < sizeof pad->axes / sizeof pad->axes[0]; i++)
            mask |= pad->axes[i].buttons;
    }
    pad->reported = mask;
    for (int i = 0; i < MAX_GAMEPADS; i++)
        mask |= pads[i].reported;
    if (mask != last_mask) {
        last_mask = mask;
        notify(mask);
    }
}
static void open_pad(const char *name, void (*notify)(unsigned)) {
    int slot = -1;
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (pads[i].fd >= 0 && !strcmp(pads[i].name, name))
            return;
        if (pads[i].fd < 0 && slot < 0)
            slot = i;
    }
    if (slot < 0 || strlen(name) >= sizeof pads[0].name)
        return;
    char path[64];
    snprintf(path, sizeof path, "%s/%s", device_dir, name);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return;
    uint8_t keys[KEY_BITS] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 ||
        !(bit_set(keys, BTN_SOUTH) || bit_set(keys, BTN_EAST) || bit_set(keys, BTN_DPAD_UP))) {
        close(fd);
        return;
    }
    pads[slot] = (struct gamepad){.fd = fd,
                                  .axes = {{ABS_X, 0, 0, PAD_BIT(LEFT), PAD_BIT(RIGHT), 0},
                                           {ABS_Y, 0, 0, PAD_BIT(UP), PAD_BIT(DOWN), 0},
                                           {ABS_HAT0X, 0, 0, PAD_BIT(LEFT), PAD_BIT(RIGHT), 0},
                                           {ABS_HAT0Y, 0, 0, PAD_BIT(UP), PAD_BIT(DOWN), 0},
                                           {ABS_Z, 0, 0, 0, PAD_BIT(L2), 0},
                                           {ABS_RZ, 0, 0, 0, PAD_BIT(R2), 0}}};
    memcpy(pads[slot].name, name, strlen(name) + 1);
    resync(&pads[slot]);
    report(&pads[slot], notify);
}
static void discover(void (*notify)(unsigned)) {
    DIR *dir = opendir(device_dir);
    if (!dir)
        return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        const char *name = ent->d_name;
        if (!strncmp(name, "event", 5) && name[5] &&
            strspn(name + 5, "0123456789") == strlen(name + 5))
            open_pad(name, notify);
    }
    closedir(dir);
}
static void handle_event(struct gamepad *pad, struct input_event ev, void (*notify)(unsigned)) {
    if (ev.type == EV_SYN) {
        if (ev.code == SYN_DROPPED)
            pad->dropped = 1;
        if (ev.code == SYN_REPORT) {
            if (pad->dropped) {
                resync(pad);
                pad->dropped = 0;
            }
            report(pad, notify);
        }
    } else if (!pad->dropped && ev.type == EV_KEY) {
        for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++) {
            if (buttons[i].code != ev.code)
                continue;
            if (ev.value)
                pad->keys |= buttons[i].bit;
            else
                pad->keys &= ~buttons[i].bit;
            break;
        }
    } else if (!pad->dropped && ev.type == EV_ABS) {
        for (size_t i = 0; i < sizeof pad->axes / sizeof pad->axes[0]; i++)
            if (pad->axes[i].code == ev.code)
                read_axis(&pad->axes[i], ev.value);
    }
}
void kz_pads_init(void) {
    for (int i = 0; i < MAX_GAMEPADS; i++)
        pads[i].fd = -1;
    watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch_fd >= 0 &&
        inotify_add_watch(watch_fd, device_dir, IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) {
        close(watch_fd);
        watch_fd = -1;
    }
    scan_count = 0;
}
void kz_pads_poll(void (*notify)(unsigned)) {
    char events[4096];
    int scan = scan_count++ == 0;
    if (watch_fd >= 0) {
        while (read(watch_fd, events, sizeof events) > 0)
            scan = 1;
    } else if (scan_count % 60 == 0)
        scan = 1;
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        struct gamepad *pad = &pads[i];
        if (pad->fd < 0)
            continue;
        struct input_event batch[32];
        ssize_t n;
        while ((n = read(pad->fd, batch, sizeof batch)) > 0)
            for (size_t j = 0; j < (size_t)n / sizeof batch[0]; j++)
                handle_event(pad, batch[j], notify);
        if (!n || (errno != EAGAIN && errno != EINTR)) {
            close(pad->fd);
            pad->fd = -1;
            report(pad, notify);
            scan = 1;
        }
    }
    if (scan)
        discover(notify);
}
void kz_pads_close(void) {
    for (int i = 0; i < MAX_GAMEPADS; i++)
        if (pads[i].fd >= 0)
            close(pads[i].fd);
    if (watch_fd >= 0)
        close(watch_fd);
}
