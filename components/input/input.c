#include "input.h"

esp_err_t input_init(void)
{
    // TODO(me):
    // 1. Pull T1/T2/button pin numbers from board.h.
    // 2. Create the event queue (xQueueCreate) that
    //    input_get_event_queue() returns.
    // 3. For each of the 3 inputs, configure a button_gpio_config_t +
    //    button_config_t (espressif/button) with the right active level -
    //    TTP223 is active HIGH (docs/DESIGN.md section 2); the push button's
    //    level is still unknown, log it during the smoke test before
    //    hardcoding BUTTON_ACTIVE_HIGH/LOW.
    // 4. Register iot_button callbacks:
    //    - T1: BUTTON_SINGLE_CLICK -> INPUT_EVENT_PREV,
    //          BUTTON_LONG_PRESS_HOLD (repeat) -> INPUT_EVENT_PREV_REPEAT.
    //    - T2: same shape with INPUT_EVENT_NEXT / INPUT_EVENT_NEXT_REPEAT.
    //    - button: BUTTON_SINGLE_CLICK -> INPUT_EVENT_ENTER,
    //          BUTTON_LONG_PRESS_START at 3s -> INPUT_EVENT_HOLD_3S (and
    //          trigger the "enter menu" beep per docs/DESIGN.md section 6.1
    //          as soon as the 3s threshold is hit, not on release).
    // 5. Handle INPUT_EVENT_BOOT_HOLD separately: check the button's level
    //    once at startup, before iot_button's own debounce would fire it as
    //    a normal event.
    // 6. Each callback should xQueueSend() the matching input_event_t.
    return ESP_OK;
}

QueueHandle_t input_get_event_queue(void)
{
    // TODO(me): return the QueueHandle_t created in input_init().
    return NULL;
}
