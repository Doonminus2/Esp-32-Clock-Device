#include "timekeeping.h"

#include "app_rtc.h"

esp_err_t timekeeping_init(void)
{
    // TODO(me):
    // 1. setenv("TZ", "ICT-7", 1); tzset(); per docs/DESIGN.md section 3.
    // 2. If app_rtc_is_valid(), read app_rtc_get_time() and feed it into the
    //    system clock via settimeofday().
    // 3. Track validity for timekeeping_is_valid() to report.
    return ESP_OK;
}

bool timekeeping_is_valid(void)
{
    // TODO(me): return the validity tracked by timekeeping_init() /
    // timekeeping_set_time() (true once either the RTC or an SNTP sync has
    // supplied a real time).
    return false;
}

void timekeeping_now(struct tm *out_time)
{
    // TODO(me):
    // 1. Call time() then localtime_r() into out_time.
}

esp_err_t timekeeping_set_time(const struct tm *time)
{
    // TODO(me):
    // 1. Convert `time` to a time_t (mktime) and apply via settimeofday().
    // 2. Call app_rtc_set_time() to persist it.
    // 3. Mark time as valid for timekeeping_is_valid().
    return ESP_OK;
}
