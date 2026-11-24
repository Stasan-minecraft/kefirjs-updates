#pragma once
#include <stdatomic.h>
#include <stdbool.h>

/* A background caller supplies a cancellation flag. A completed cache produces
   no background notice. The foreground System screen always receives a status. */
const char *k_native_source(bool refresh, const atomic_bool *cancel);
