#ifndef KEFIRIZ_DRAW_H
#define KEFIRIZ_DRAW_H

#include <stddef.h>
#include <stdint.h>

#define KZ_WIDTH 1280
#define KZ_HEIGHT 720
#define KZ_TILE_COUNT 4
#define KZ_TOOL_COUNT 6

typedef enum {
    KZ_ICON_NONE, KZ_ICON_DOWNLOAD, KZ_ICON_LIBRARY, KZ_ICON_ALBUM,
    KZ_ICON_CONTROLLER, KZ_ICON_SETTINGS, KZ_ICON_POWER, KZ_ICON_PROFILES,
    KZ_ICON_SOFTWARE, KZ_ICON_NETWORK, KZ_ICON_BACK, KZ_ICON_PLAY
} KzIcon;

typedef struct {
    const char *title;
    KzIcon icon;
} KzTile;

typedef struct {
    const char *label, *value;
    KzIcon icon;
    int disabled;
} KzRow;

typedef struct {
    int light;
    const char *clock, *selected_title;
    KzTile tiles[KZ_TILE_COUNT];
    KzIcon tools[KZ_TOOL_COUNT];
    int tile_focus, tool_focus;
    const char *left_hint, *right_hint;
    const char *window_title, *window_detail;
    const KzRow *rows;
    int row_count, row_focus;
} KzScene;

/* pitch_pixels is measured in uint32_t pixels. The surface is 1280 x 720 XRGB8888. */
void kz_draw(uint32_t *pixels, size_t pitch_pixels, const KzScene *scene);
/* 0..3 tile, 10..15 tool, 20 + absolute list row, -2 close, -1 no target. */
int kz_hit(const KzScene *scene, int x, int y);
void kz_keyboard(uint32_t *pixels, size_t pitch_pixels, int light,
                 const char *masked, const char *keys, int focused);
int kz_key_hit(int x, int y);

#endif
