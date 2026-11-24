#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char current[24], latest[24], notes[1024], message[256];
    uint64_t bytes;
    bool preview, profiles_off;
} KfUpdate;

typedef void (*KfProgress)(const char *, uint64_t, uint64_t, void *);
enum { KF_ERROR = -1, KF_CURRENT = 0, KF_AVAILABLE = 1, KF_APPLIED = 2, KF_RECOVERED = 3, KF_OFFLINE = 4 };

/* One worker owns the engine between Check and Apply. Progress runs on that worker. */
void kf_local(KfUpdate *update);
bool kf_set_preview(bool enabled);
bool kf_enable_profiles(void);
int kf_check(KfUpdate *update, KfProgress progress, void *user);
int kf_apply(KfUpdate *update, KfProgress progress, void *user);
int kf_recover(KfUpdate *update, KfProgress progress, void *user);
