// Feed Linux input packets through the same nonblocking reader as a controller.
#include "../native/desktop_input.c"
#include <assert.h>

static unsigned history[32];
static size_t samples;

static void capture(unsigned mask) {
    assert(samples < sizeof history / sizeof history[0]);
    history[samples++] = mask;
}

static void packet(int fd, unsigned type, unsigned code, int value) {
    const struct input_event events[] = {
        {.type = type, .code = code, .value = value},
        {.type = EV_SYN, .code = SYN_REPORT},
    };
    assert(write(fd, events, sizeof events) == (ssize_t)sizeof events);
}

int main(void) {
    int left[2], right[2];
    assert(pipe(left) == 0 && pipe(right) == 0);
    assert(fcntl(left[0], F_SETFL, O_NONBLOCK) == 0);
    assert(fcntl(right[0], F_SETFL, O_NONBLOCK) == 0);
    for (size_t i = 0; i < MAX_GAMEPADS; i++)
        pads[i].fd = -1;
    pads[0].fd = left[0];
    pads[1].fd = right[0];
    pads[0].axes[0] = (struct axis){ABS_X, -32768, 32767, PAD_BIT(LEFT), PAD_BIT(RIGHT), 0};
    pads[0].axes[1] = (struct axis){ABS_HAT0X, -1, 1, PAD_BIT(LEFT), PAD_BIT(RIGHT), 0};
    pads[1].axes[0] = (struct axis){ABS_RZ, 0, 255, 0, PAD_BIT(R2), 0};
    scan_count = 1;

    packet(left[1], EV_KEY, BTN_DPAD_UP, 1);
    packet(right[1], EV_KEY, BTN_EAST, 1);
    packet(right[1], EV_KEY, BTN_EAST, 0);
    kz_pads_poll(capture);
    assert(samples == 3 && history[0] == PAD_BIT(UP));
    assert(history[1] == (PAD_BIT(UP) | PAD_BIT(A)) && history[2] == PAD_BIT(UP));
    packet(left[1], EV_KEY, BTN_DPAD_UP, 0);
    packet(left[1], EV_ABS, ABS_X, -23000);
    packet(left[1], EV_ABS, ABS_HAT0X, -1);
    packet(left[1], EV_ABS, ABS_X, 1200);
    packet(left[1], EV_ABS, ABS_HAT0X, 0);
    kz_pads_poll(capture);
    assert(samples == 6 && history[3] == 0 && history[4] == PAD_BIT(LEFT) && history[5] == 0);
    packet(right[1], EV_KEY, BTN_TR, 1);
    packet(right[1], EV_KEY, BTN_TR2, 1);
    packet(right[1], EV_ABS, ABS_RZ, 255);
    packet(right[1], EV_KEY, BTN_TR2, 0);
    packet(right[1], EV_ABS, ABS_RZ, 0);
    kz_pads_poll(capture);
    assert(samples == 9 && history[6] == PAD_BIT(R));
    assert(history[7] == (PAD_BIT(R) | PAD_BIT(R2)) && history[8] == PAD_BIT(R));
    close(right[1]);
    kz_pads_poll(capture);
    assert(samples == 10 && history[9] == 0);
    close(left[1]);
    kz_pads_close();
    return 0;
}
