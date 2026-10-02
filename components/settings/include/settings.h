#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * Everything the settings menu (docs/DESIGN.md section 7) persists in NVS.
 * Time and date are NOT in here - those live on the RTC via timekeeping.
 */
typedef struct {
    bool use_24h;             // 12H vs 24H display format
    bool night_mode_enabled;  // auto-dim window on/off
    uint8_t night_start_hour; // 0-23, default 22
    uint8_t night_end_hour;   // 0-23, default 6
    uint8_t brightness;       // 1-8, see docs/DESIGN.md section 2 current limit
    uint8_t alarm_hour;       // 0-23
    uint8_t alarm_minute;     // 0-59
    bool alarm_enabled;
} settings_t;

/**
 * Opens NVS and loads settings, applying defaults for any key that isn't
 * present yet (first boot). Must be called once before settings_get().
 */
esp_err_t settings_init(void);

/** Copies the current in-memory settings into *out. */
void settings_get(settings_t *out);

/** Validates and persists `settings` to NVS, replacing the current values. */
esp_err_t settings_set(const settings_t *settings);
