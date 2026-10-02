#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Raw input events per docs/DESIGN.md section 6.1. This component only
 * produces these - it never calls into ui directly (driver layers don't
 * know about ui, per docs/DESIGN.md section 4 dependency rules).
 */
typedef enum {
    INPUT_EVENT_PREV,        // T1 tap: previous mode / -1 in menu
    INPUT_EVENT_NEXT,        // T2 tap: next mode / +1 in menu
    INPUT_EVENT_PREV_REPEAT, // T1 held in menu: fast -1 repeat
    INPUT_EVENT_NEXT_REPEAT, // T2 held in menu: fast +1 repeat
    INPUT_EVENT_ENTER,       // push button short press
    INPUT_EVENT_HOLD_3S,     // push button held 3s: enter settings menu
    INPUT_EVENT_BOOT_HOLD,   // push button held while powering on: WiFi setup
} input_event_t;

/**
 * Initializes T1, T2, and the push button via espressif/button with
 * debounce and the hold timings from docs/DESIGN.md section 6.1, and starts
 * dispatching input_event_t values to the queue returned by
 * input_get_event_queue().
 */
esp_err_t input_init(void);

/** Queue of input_event_t values, consumed by ui. */
QueueHandle_t input_get_event_queue(void);
