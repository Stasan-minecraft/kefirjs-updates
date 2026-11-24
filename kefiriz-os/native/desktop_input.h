#ifndef KEFIRIZ_DESKTOP_INPUT_H
#define KEFIRIZ_DESKTOP_INPUT_H

#include <stdint.h>

uint64_t kz_frame_generation(void);
void kz_pads_init(void);
void kz_pads_poll(void (*on_buttons)(unsigned mask));
void kz_pads_close(void);

#endif
