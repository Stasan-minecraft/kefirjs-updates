#include "system_ui.h"
#include "native_source.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void read_snapshot(SystemSnapshot *s) {
    *s = (SystemSnapshot){.release = "Unavailable",
                          .edition = "No valid release record",
                          .horizon = "Unavailable",
                          .atmosphere = "Unavailable",
                          .environment = "Unavailable",
                          .color = "Unavailable",
                          .context = "Unavailable",
                          .storage = "Unavailable"};
    FILE *f = fopen("sdmc:/config/kefiriz/release.txt", "rb");
    if (f) {
        char line[112];
        size_t n = fread(line, 1, sizeof(line), f);
        if (!ferror(f) && n < sizeof(line))
            k_release_line(line, n, s);
        fclose(f);
    }
    u32 h = hosversionGet();
    if (h)
        snprintf(s->horizon, sizeof(s->horizon), "%u.%u.%u", HOSVER_MAJOR(h), HOSVER_MINOR(h),
                 HOSVER_MICRO(h));
    if (R_SUCCEEDED(setsysInitialize())) {
        ColorSetId color;
        if (R_SUCCEEDED(setsysGetColorSetId(&color)) &&
            (color == ColorSetId_Dark || color == ColorSetId_Light)) {
            s->dark = color == ColorSetId_Dark;
            strcpy(s->color, s->dark ? "Basic Black" : "Basic White");
        }
        setsysExit();
    }
    if (hosversionIsAtmosphere() && R_SUCCEEDED(splInitialize())) {
        u64 raw;
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65000, &raw)))
            snprintf(s->atmosphere, sizeof(s->atmosphere), "%u.%u.%u", (unsigned)((raw >> 56) & 255),
                     (unsigned)((raw >> 48) & 255), (unsigned)((raw >> 40) & 255));
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65007, &raw)) && raw <= 2)
            strcpy(s->environment, raw ? "Emulated eMMC" : "System eMMC");
        splExit();
    }
    AppletType t = appletGetAppletType();
    if (t == AppletType_Application || t == AppletType_SystemApplication)
        strcpy(s->context, "Full application");
    else if (t == AppletType_LibraryApplet)
        strcpy(s->context, "Applet");
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    s64 free_bytes = 0;
    if (sd && R_SUCCEEDED(fsFsGetFreeSpace(sd, "/", &free_bytes)) && free_bytes >= 0)
        snprintf(s->storage, sizeof(s->storage), "%.1f GiB free", (double)free_bytes / 1073741824.);
}
static bool open_updater(void) {
    static const char path[] = "sdmc:/switch/KEFIRJS-Updater/KEFIRJS-Updater.nro";
    return envHasNextLoad() && access(path, R_OK) == 0 && R_SUCCEEDED(envSetNextLoad(path, path));
}

static void fallback(void) {
    SystemSnapshot s;
    read_snapshot(&s);
    consoleInit(NULL);
    printf("\n  KEFIRIZ System\n\n  %s\n  %s\n\n  Horizon OS    %s\n  Atmosphere    %s\n  Environment  "
           " %s\n  SD card       %s\n\n  X   Updater    A   Refresh    B   Close\n",
           s.release, s.edition, s.horizon, s.atmosphere, s.environment, s.storage);
    printf("\n  Checking native HOME resources...\n");
    consoleUpdate(NULL);
    printf("  %s\n", k_native_source(false, NULL));
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_X) {
            if (open_updater()) break;
            printf("  Could not open KEFIRIZ Updater.\n");
        }
        if (down & (HidNpadButton_B | HidNpadButton_Plus))
            break;
        if (down & HidNpadButton_A) printf("  %s\n", k_native_source(true, NULL));
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}
int main(void) {
    PlFontData data;
    if (R_FAILED(plInitialize(PlServiceType_User))) {
        fallback();
        return 0;
    }
    if (R_FAILED(plGetSharedFontByType(&data, PlSharedFontType_Standard)) ||
        !k_font_init(data.address)) {
        plExit();
        fallback();
        return 0;
    }
    Framebuffer fb;
    if (R_FAILED(framebufferCreate(&fb, nwindowGetDefault(), K_WIDTH, K_HEIGHT, PIXEL_FORMAT_RGBA_8888,
                                   2))) {
        plExit();
        fallback();
        return 0;
    }
    if (R_FAILED(framebufferMakeLinear(&fb))) {
        framebufferClose(&fb);
        plExit();
        fallback();
        return 0;
    }
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    SystemSnapshot s;
    bool refresh = true, touch_active = false, updater_error = false;
    const char *native_status = "Checking native HOME resources...";
    bool capture_pending = true, force_capture = false;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        HidTouchScreenState touch = {0};
        bool touching = hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0;
        int action =
            (touching && !touch_active) ? k_footer_hit(touch.touches[0].x, touch.touches[0].y) : 0;
        touch_active = touching;
        if ((down & (HidNpadButton_B | HidNpadButton_Plus)) || action == 2)
            break;
        if ((down & HidNpadButton_X) || action == 3) {
            if (open_updater()) break;
            updater_error = true;
            refresh = true;
        }
        if ((down & HidNpadButton_A) || action == 1) {
            refresh = true;
            updater_error = false;
            capture_pending = true;
            force_capture = true;
            native_status = "Reading native HOME resources...";
        }
        if (refresh) {
            read_snapshot(&s);
            s.updater_error = updater_error;
            s.native_source = native_status;
            u32 stride;
            u32 *pixels = framebufferBegin(&fb, &stride);
            k_draw(pixels, (int)(stride / sizeof(*pixels)), &s);
            framebufferEnd(&fb);
            refresh = false;
            if (capture_pending) {
                native_status = k_native_source(force_capture, NULL);
                capture_pending = false;
                refresh = true;
            }
        } else
            svcSleepThread(16000000);
    }
    framebufferClose(&fb);
    plExit();
    return 0;
}
