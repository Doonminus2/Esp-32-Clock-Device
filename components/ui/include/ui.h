#pragma once

#include "esp_err.h"

/** UI FSM states per docs/DESIGN.md section 8.1. */
typedef enum {
    UI_STATE_BOOT,
    UI_STATE_WAIT_TIME,
    UI_STATE_MODE_CLOCK,
    UI_STATE_MODE_DATE,
    UI_STATE_MODE_STOPWATCH,
    UI_STATE_MODE_ALARM,
    UI_STATE_SHOW_NET_STATUS,
    UI_STATE_MENU,
    UI_STATE_ALARM_RINGING,
    UI_STATE_PROVISIONING,
} ui_state_t;

/**
 * Starts the UI FSM task (50 ms tick, docs/DESIGN.md section 8.1). This is
 * the only component allowed to know about display, input, buzzer,
 * settings, net, and timekeeping all at once - call it last, after every
 * other module's init has run.
 */
esp_err_t ui_init(void);

/** Current UI FSM state, mainly for logging/debugging. */
ui_state_t ui_get_state(void);
