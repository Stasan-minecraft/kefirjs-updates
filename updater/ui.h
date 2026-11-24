#pragma once
#include "engine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { UP_WIDTH = 1280, UP_HEIGHT = 720, UP_ROWS = 8 };
typedef enum {
    UP_READY, UP_CHECKING, UP_CURRENT, UP_AVAILABLE, UP_CONFIRM, UP_INSTALLING,
    UP_INSTALLED, UP_RECOVERED, UP_ERROR, UP_RECOVERING, UP_OFFLINE
} UpScreen;
typedef enum { UP_NONE, UP_CHECK, UP_APPLY, UP_CLOSE } UpCommand;
typedef enum { UP_PRIMARY, UP_BACK, UP_PREVIOUS, UP_NEXT } UpInput;
typedef struct {
    KfUpdate release;
    UpScreen screen;
    bool dark, compatible;
    char phase[112];
    uint64_t done, total;
    unsigned page;
} UpView;

bool up_busy(UpScreen screen);
UpCommand up_input(UpView *view, UpInput input);
const char *up_title(UpScreen screen);
const char *up_primary(UpScreen screen);
size_t up_lines(const UpView *view, char (*lines)[64], size_t cap);
unsigned up_pages(const UpView *view);
void up_result(UpView *view, int result);
bool up_font(const unsigned char *data);
void up_draw(uint32_t *pixels, int stride, const UpView *view);
int up_touch(int x, int y);
