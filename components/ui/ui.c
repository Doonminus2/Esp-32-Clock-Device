#include "ui.h"

#include "buzzer.h"
#include "display.h"
#include "input.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

esp_err_t ui_init(void)
{
    // TODO(me):
    // 1. Read timekeeping_is_valid() to pick the boot transition in
    //    docs/DESIGN.md section 8.1: valid -> UI_STATE_MODE_CLOCK,
    //    invalid -> UI_STATE_WAIT_TIME.
    // 2. Create a FreeRTOS task (xTaskCreate) that ticks every 50 ms:
    //    - drains input_get_event_queue() and applies the UI FSM table
    //      (section 8.1) to decide the next state,
    //    - redraws a display_framebuffer_t for the current state/mode
    //      (sections 5.1-5.8) and calls display_flush(),
    //    - checks the alarm time against timekeeping_now() + settings_get()
    //      and jumps to UI_STATE_ALARM_RINGING when it hits and the alarm
    //      is enabled, driving buzzer_play(BUZZER_PATTERN_ALARM) /
    //      buzzer_stop() around that state,
    //    - applies settings_get()'s night-mode window to
    //      display_set_brightness().
    // 3. Store the resulting state for ui_get_state() to read.
    return ESP_OK;
}

ui_state_t ui_get_state(void)
{
    // TODO(me): return the state tracked by the UI task.
    return UI_STATE_BOOT;
}
