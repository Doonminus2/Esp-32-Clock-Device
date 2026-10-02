#include "buzzer.h"

esp_err_t buzzer_init(void)
{
    // TODO(me):
    // 1. Pull the buzzer pin from board.h.
    // 2. Configure an LEDC timer (ledc_timer_config_t) and channel
    //    (ledc_channel_config_t) on that pin for a passive buzzer tone.
    // 3. Leave duty at 0 (silent) until buzzer_play() is called.
    return ESP_OK;
}

esp_err_t buzzer_play(buzzer_pattern_t pattern)
{
    // TODO(me):
    // 1. Switch on `pattern` and drive the LEDC duty/frequency to produce
    //    the matching beep shape from docs/DESIGN.md section 6.3.
    // 2. For BUZZER_PATTERN_MENU_ENTER / WIFI_ENTER / SAVE: short blocking
    //    (or timer-scheduled) beep(s), then duty back to 0.
    // 3. For BUZZER_PATTERN_ALARM: start a repeating beep-beep and return
    //    immediately - something (an esp_timer or a task) needs to keep
    //    repeating until buzzer_stop() is called.
    return ESP_OK;
}

esp_err_t buzzer_stop(void)
{
    // TODO(me):
    // 1. Cancel whatever is driving BUZZER_PATTERN_ALARM's repeat (timer or
    //    task).
    // 2. Set LEDC duty to 0.
    return ESP_OK;
}
