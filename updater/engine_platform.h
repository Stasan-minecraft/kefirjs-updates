#pragma once
#include "engine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
/* Both GitHub endpoints serve the application's fixed repository. */
const char *kf_network_error(void);
typedef bool (*KfSink)(const void *bytes, size_t count, void *user);
bool kf_fetch(const char *relative, KfSink sink, void *user, size_t limit, KfProgress cb, void *ctx);
bool kf_horizon_ok(void);
bool kf_hash(FILE *file, char digest[65]);
bool kf_flush(FILE *file);
bool kf_free(uint64_t *bytes);
/* Kernel resources held until release, or released when the process exits. */
bool kf_lock(void);
void kf_unlock(void);
