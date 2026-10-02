#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

/**
 * Sets the TZ environment variable (ICT-7 per docs/DESIGN.md section 3) and
 * pulls the initial system time from the RTC. Must be called after
 * app_rtc_init(), before anything reads timekeeping_now().
 */
esp_err_t timekeeping_init(void);

/**
 * True if the system clock is trustworthy: the RTC had a valid time at
 * boot, or a SNTP sync has completed since (see net's SNTP_SYNCING state).
 */
bool timekeeping_is_valid(void);

/** Returns the current local time (already TZ-adjusted). */
void timekeeping_now(struct tm *out_time);

/**
 * Sets the system clock and persists the new time to the RTC so it survives
 * a power cycle. Used both by net (after SNTP sync) and by the settings
 * menu (manual TIME/DATE entry, docs/DESIGN.md section 7).
 */
esp_err_t timekeeping_set_time(const struct tm *time);
