#pragma once

#include "esp_err.h"

/** Beep patterns per docs/DESIGN.md section 6.3. */
typedef enum {
    BUZZER_PATTERN_MENU_ENTER, // entering settings menu (hold 3s): 1 short beep
    BUZZER_PATTERN_WIFI_ENTER, // entering WiFi setup: 2 short beeps
    BUZZER_PATTERN_SAVE,       // menu value saved: 1 very short beep
    BUZZER_PATTERN_ALARM,      // alarm ringing: repeating beep-beep until stopped
} buzzer_pattern_t;

/** Configures the LEDC PWM channel on the buzzer pin from board.h. */
esp_err_t buzzer_init(void);

/**
 * Plays a pattern. BUZZER_PATTERN_ALARM repeats until buzzer_stop() is
 * called (ui is responsible for the 60s cap in docs/DESIGN.md section 8.1);
 * the other patterns are short one-shots.
 */
esp_err_t buzzer_play(buzzer_pattern_t pattern);

/** Immediately silences the buzzer, interrupting any in-progress pattern. */
esp_err_t buzzer_stop(void);
