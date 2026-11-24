#include "ui.h"
#include <stdio.h>
#include <string.h>

bool up_busy(UpScreen s) {
    return s == UP_CHECKING || s == UP_INSTALLING || s == UP_RECOVERING;
}
const char *up_title(UpScreen s) {
    static const char *const titles[] = {
        "Firmware updates", "Checking for updates", "You're up to date",
        "An update is available", "Install this update?", "Installing update",
        "Update installed", "Update recovered", "Update needs attention", "Checking SD card", "Online check unavailable",
        "Installed version is newer", "Profiles enabled"
    };
    return titles[s];
}
const char *up_primary(UpScreen s) {
    switch (s) {
    case UP_AVAILABLE: return "Review update";
    case UP_CONFIRM: return "Install update";
    case UP_OFFLINE:
    case UP_ERROR: return "Try again";
    case UP_INSTALLED: return "Close";
    default: return up_busy(s) ? "" : "Check for updates";
    }
}
void up_result(UpView *v, int rc) {
    switch (rc) {
    case KF_OFFLINE: v->screen = UP_OFFLINE; break;
    case KF_CURRENT: v->screen = strcmp(v->release.current, v->release.latest) && v->release.latest[0] ? UP_AHEAD : UP_CURRENT; break;
    case KF_AVAILABLE: v->screen = UP_AVAILABLE; break;
    case KF_APPLIED: v->screen = UP_INSTALLED; break;
    case KF_RECOVERED: v->screen = UP_RECOVERED; break;
    default: v->screen = UP_ERROR; break;
    }
    v->page = 0;
    v->done = v->total = 0;
    v->phase[0] = 0;
}
static const char *description(const UpView *v) {
    if (v->screen == UP_PROFILES_ENABLED)
        return "Restart the Switch to use R + ZR in HOME and games.";
    if (v->screen == UP_CONFIRM)
        return "Install the selected release on this microSD? Keep the console powered and the card inserted until the update finishes.";
    if (v->screen == UP_INSTALLED)
        return "Close the updater. Your updated Homebrew menu and applications are ready to open.";
    if (v->screen == UP_READY)
        return "Check the selected channel for firmware updates.";
    if (v->screen == UP_RECOVERING)
        return "Reading the update history on your microSD.";
    if (v->screen == UP_CHECKING)
        return "Retrieving the latest release from GitHub.";
    if (v->screen == UP_INSTALLING)
        return "Keep the console powered and the microSD inserted.";
    return v->release.message;
}
// Release notes use printable English. Wrap complete words and preserve paragraphs.
static void wrap(const char *s, char (*lines)[64], size_t cap, size_t *used) {
    while (s && *s && *used < cap) {
        if (*s == '\r') { ++s; continue; }
        if (*s == '\n') { lines[(*used)++][0] = 0; ++s; continue; }
        while (*s == ' ' || *s == '\t') ++s;
        if (!*s) break;
        size_t n = strcspn(s, "\r\n");
        size_t end = n < 62 ? n : 62;
        if (end < n && s[end] != ' ') {
            size_t cut = end;
            while (cut && s[cut] != ' ') --cut;
            if (cut) end = cut;
        }
        memcpy(lines[*used], s, end);
        lines[(*used)++][end] = 0;
        s += end;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '\r') ++s;
        if (*s == '\n') ++s;
    }
}
size_t up_lines(const UpView *v, char (*lines)[64], size_t cap) {
    size_t used = 0;
    bool release = v->screen == UP_AVAILABLE || v->screen == UP_CURRENT;
    if (!release || !v->release.notes[0])
        wrap(description(v), lines, cap, &used);
    if (release && v->release.notes[0])
        wrap(v->release.notes, lines, cap, &used);
    if (v->screen == UP_AVAILABLE && !v->compatible) {
        if (used < cap) lines[used++][0] = 0;
        wrap("This edition requires Horizon 20.5.0.", lines, cap, &used);
    }
    return used;
}
unsigned up_pages(const UpView *v) {
    char lines[64][64];
    size_t n = up_lines(v, lines, 64);
    return n ? (unsigned)((n + UP_ROWS - 1) / UP_ROWS) : 1;
}
UpCommand up_input(UpView *v, UpInput input) {
    if (input == UP_PREVIOUS) {
        if (v->page) --v->page;
    } else if (input == UP_NEXT) {
        if (v->page + 1 < up_pages(v)) ++v->page;
    } else if (!up_busy(v->screen)) {
        if (input == UP_CHANNEL && v->screen != UP_CONFIRM) return UP_SWITCH_CHANNEL;
        if (input == UP_PROFILES && v->screen != UP_CONFIRM && v->compatible && v->release.profiles_off)
            return UP_ENABLE_PROFILES;
        if (input == UP_BACK) {
            if (v->screen != UP_CONFIRM) return UP_CLOSE;
            v->screen = UP_AVAILABLE;
            v->page = 0;
        } else if (input == UP_PRIMARY) {
            if (v->screen == UP_INSTALLED) return UP_CLOSE;
            if (v->screen == UP_CONFIRM) return v->compatible ? UP_APPLY : UP_NONE;
            if (v->screen != UP_AVAILABLE) return UP_CHECK;
            if (v->compatible) {
                v->page = 0;
                v->screen = UP_CONFIRM;
            }
        }
    }
    return UP_NONE;
}

int up_touch(int x, int y) {
    if (y >= 592 && y < 678 && x >= 580 && x < 865) return UP_PROFILES;
    if (y >= 592 && y < 678 && x >= 285 && x < 575) return UP_CHANNEL;
    if (y >= 592 && y < 678 && x >= 885 && x < 1224) return UP_PRIMARY;
    if (y >= 592 && y < 678 && x >= 51 && x < 212) return UP_BACK;
    if (y >= 543 && y < 590 && x >= 941 && x < 1036) return UP_PREVIOUS;
    if (y >= 543 && y < 590 && x >= 1121 && x < 1224) return UP_NEXT;
    return -1;
}
