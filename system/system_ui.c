#include "system_ui.h"
#include "emblem.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"

static stbtt_fontinfo font;
static uint32_t *buf;
static int stride;

static uint32_t rgb(unsigned c) { return 0xff000000u | ((c & 255) << 16) | (c & 0xff00) | (c >> 16); }
static void rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            buf[j * stride + i] = color;
}
static uint32_t over(uint32_t bg, uint32_t ink, int alpha) {
    unsigned v = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        unsigned a = (bg >> shift) & 255, b = (ink >> shift) & 255;
        v |= ((a * (255 - alpha) + b * alpha + 127) / 255) << shift;
    }
    return v;
}
bool k_font_init(const unsigned char *data) {
    if (!data)
        return false;
    int first = stbtt_GetFontOffsetForIndex(data, 0);
    return first >= 0 && stbtt_InitFont(&font, data, first);
}
static int width(const char *text, float scale) {
    float x = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int advance;
        stbtt_GetCodepointHMetrics(&font, *p, &advance, NULL);
        x += advance * scale;
        if (p[1])
            x += stbtt_GetCodepointKernAdvance(&font, *p, p[1]) * scale;
    }
    return (int)ceilf(x);
}
static void label(int x, int base, int size, const char *text, uint32_t color, int limit, bool right) {
    float scale = stbtt_ScaleForPixelHeight(&font, (float)size);
    int w = width(text, scale);
    if (w > limit && w > 0)
        scale *= (float)limit / (float)w;
    float pen = (float)(right ? x - width(text, scale) : x);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int gw = 0, gh = 0, dx = 0, dy = 0, advance = 0;
        unsigned char *mask = stbtt_GetCodepointBitmap(&font, scale, scale, *p, &gw, &gh, &dx, &dy);
        if (mask) {
            int bx = (int)roundf(pen) + dx, by = base + dy;
            for (int j = 0; j < gh; j++)
                for (int i = 0; i < gw; i++) {
                    int xx = bx + i, yy = by + j;
                    if ((unsigned)xx < K_WIDTH && (unsigned)yy < K_HEIGHT && mask[j * gw + i]) {
                        uint32_t *dst = &buf[yy * stride + xx];
                        *dst = over(*dst, color, mask[j * gw + i]);
                    }
                }
            stbtt_FreeBitmap(mask, NULL);
        }
        stbtt_GetCodepointHMetrics(&font, *p, &advance, NULL);
        pen += advance * scale;
        if (p[1])
            pen += stbtt_GetCodepointKernAdvance(&font, *p, p[1]) * scale;
    }
}
static void emblem(int x, int y) {
    for (size_t row = 0; row < sizeof(emblem_inset); ++row) {
        int inset = emblem_inset[row];
        int end = (int)sizeof(emblem_pixels[row]) - inset;
        uint32_t *dest = buf + (y + (int)row) * stride + x;
        for (int col = inset; col < end; ++col)
            dest[col] = emblem_palette[emblem_pixels[row][col]];
    }
}
bool k_release_line(const char *line, size_t n, SystemSnapshot *s) {
    if (!line || !n || n >= 112)
        return false;
    if (line[n - 1] == '\n')
        --n;
    if (n && line[n - 1] == '\r')
        --n;
    if (n < 10 || memcmp(line, "KEFIRIZ ", 8))
        return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)line[i] < 32 || (unsigned char)line[i] > 126)
            return false;
    const char separator[] = " / ";
    size_t cut = 8;
    while (cut + 3 <= n && memcmp(line + cut, separator, 3))
        cut++;
    if (cut + 3 >= n || cut < 9 || cut >= sizeof(s->release) || n - cut - 3 >= sizeof(s->edition))
        return false;
    memcpy(s->release, line, cut);
    s->release[cut] = 0;
    memcpy(s->edition, line + cut + 3, n - cut - 3);
    s->edition[n - cut - 3] = 0;
    if (!strcmp(s->edition, "Atmosphere edition")) {
        if (!strcmp(s->release, "KEFIRIZ 2.1.0")) strcpy(s->release, "KEFIRIZ Aurora 2.1.0");
        if (!strcmp(s->release, "KEFIRIZ 2.1.1")) strcpy(s->release, "KEFIRIZ Aurora 2.1.1");
        if (!strcmp(s->release, "KEFIRIZ 2.2.0")) strcpy(s->release, "KEFIRIZ Meridian 2.2.0");
    }
    return true;
}
int k_footer_hit(int x, int y) {
    if (y < 605 || y >= 695)
        return 0;
    if (x >= 550 && x < 802)
        return 3;
    if (x >= 802 && x < 1032)
        return 1;
    if (x >= 1032 && x < 1230)
        return 2;
    return 0;
}
void k_draw(uint32_t *pixels, int step, const SystemSnapshot *s) {
    buf = pixels;
    stride = step;
    uint32_t paper = rgb(s->dark ? 0x101820 : 0xeef5f2);
    uint32_t ink = rgb(s->dark ? 0xecf7f5 : 0x152a35);
    uint32_t secondary = rgb(s->dark ? 0xa7bdc4 : 0x4d636b);
    uint32_t rule = rgb(s->dark ? 0x364952 : 0xbdcecc);
    uint32_t panel = rgb(s->dark ? 0x1c2b37 : 0xdce9e7);
    uint32_t accent = rgb(s->dark ? 0x94f1cf : 0x006d60);
    rect(0, 0, K_WIDTH, K_HEIGHT, paper);
    rect(0, 0, K_WIDTH, 81, rgb(0x152a36));
    rect(54, 34, 5, 22, rgb(0x94f1cf));
    rect(63, 29, 5, 22, rgb(0x648bff));
    label(84, 58, 28, "KEFIRIZ  /  MERIDIAN", rgb(0xe6f6f2), 420, false);
    label(54, 153, 67, "System information", ink, 1168, false);
    rect(54, 178, 446, 357, rgb(0x172c3b));
    for (int i = 0; i < 75; i++)
        rect(375+i, 268-i, 30, 2, rgb(0x234a5a));
    emblem(81, 203);
    label(81, 357, 24, "Installed release", rgb(0xb6ccd0), 385, false);
    label(79, 417, 47, s->release, rgb(0xeaf6f3), 391, false);
    rect(81, 451, 389, 1, rgb(0x44626c));
    label(81, 500, 29, s->edition, rgb(0x94f1cf), 388, false);
    const char *keys[] = {"Horizon OS",   "Atmosphere",      "Environment",
                          "System color", "Homebrew memory", "SD card"};
    const char *values[] = {s->horizon, s->atmosphere, s->environment,
                            s->color,   s->context,    s->storage};
    for (int i = 0; i < 6; i++) {
        int top = 178 + 122 * (i / 2), left = 520 + 356 * (i % 2);
        rect(left, top, 344, 113, panel);
        label(left+20, top+38, 23, keys[i], secondary, 306, false);
        label(left+20, top+83, 36, values[i], ink, 306, false);
    }
    if (s->native_source)
        label(64, 590, 24, s->native_source, secondary, 1152, false);
    rect(54, 608, 1172, 2, rule);
    if (s->updater_error)
        label(64, 665, 23, "Could not open KEFIRIZ Updater", accent, 470, false);
    label(575, 665, 28, "X   Updater", ink, 205, false);
    label(832, 665, 28, "A   Refresh", ink, 185, false);
    label(1070, 665, 28, "B   Close", ink, 151, false);
}
