#include "app_rtc.h"

esp_err_t app_rtc_init(void)
{
    // TODO(me):
    // 1. Pull CLK/DAT/RST pin numbers from board.h.
    // 2. Build a ds1302_t descriptor and call ds1302_init_desc() (see
    //    managed_components/esp-idf-lib__ds1302/examples/default/main/main.c
    //    for the expected call shape).
    return ESP_OK;
}

esp_err_t app_rtc_get_time(struct tm *out_time)
{
    // TODO(me):
    // 1. Call ds1302_get_time() into out_time.
    // 2. Propagate its esp_err_t.
    return ESP_OK;
}

esp_err_t app_rtc_set_time(const struct tm *time)
{
    // TODO(me):
    // 1. Call ds1302_set_time() with the given struct tm.
    // 2. Propagate its esp_err_t.
    return ESP_OK;
}

bool app_rtc_is_valid(void)
{
    // TODO(me):
    // 1. Check the DS1302 clock-halt flag (ds1302 library exposes this, or
    //    read the seconds register directly per its datasheet).
    // 2. Optionally sanity-check the returned struct tm fields are in range.
    return false;
}
