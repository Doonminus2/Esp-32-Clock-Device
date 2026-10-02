#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

// Named app_rtc, not rtc: ESP-IDF's esp_hw_support already defines a global
// C function rtc_init() (soc/rtc.h), which collides at link time with a
// bare rtc_init() here. See docs/DESIGN.md section 4.

/**
 * Brings up the DS1302 bus (CLK/DAT/RST pins from board.h).
 * Must be called once before any other app_rtc_* function.
 */
esp_err_t app_rtc_init(void);

/** Reads the current date/time from the DS1302 into *out_time. */
esp_err_t app_rtc_get_time(struct tm *out_time);

/**
 * Writes date/time to the DS1302. Callers in the settings menu always pass
 * tm_sec = 0 per docs/DESIGN.md section 7 ("ตั้งเวลา ... วินาที = 00").
 */
esp_err_t app_rtc_set_time(const struct tm *time);

/**
 * Returns true if the DS1302 currently holds a plausible time (chip not in
 * clock-halt state, fields in valid ranges) - false means the clock needs
 * to be set, e.g. after first power-up or a dead backup battery.
 */
bool app_rtc_is_valid(void);
