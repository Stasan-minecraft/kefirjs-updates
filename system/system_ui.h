#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { K_WIDTH = 1280, K_HEIGHT = 720 };
typedef struct {
    char release[64], edition[64], horizon[32], atmosphere[32], environment[48];
    char color[32], context[32], storage[48];
    const char *native_source;
    bool dark, updater_error;
} SystemSnapshot;
bool k_font_init(const unsigned char *font_data);
void k_draw(uint32_t *pixels, int stride_pixels, const SystemSnapshot *s);
bool k_release_line(const char *line, size_t n, SystemSnapshot *s);
int k_footer_hit(int x, int y);
