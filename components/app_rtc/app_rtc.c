#include "app_rtc.h"

#include "board.h"
#include "ds1302.h"
#include "esp_log.h"

static const char *TAG = "app_rtc";

// Any time outside this range is treated as garbage (e.g. module not
// connected, or the chip lost power and came back with random values).
#define RTC_MIN_YEAR 2024
#define RTC_MAX_YEAR 2099

// The DS1302 pins. The library calls the RST pin "CE" (chip enable).
static ds1302_t s_dev = {
    .ce_pin = BOARD_PIN_RTC_RST,
    .io_pin = BOARD_PIN_RTC_DAT,
    .sclk_pin = BOARD_PIN_RTC_CLK,
};
static bool s_ready = false; // true once app_rtc_init() succeeded

// Returns true if every field of t is inside its normal range.
static bool tm_fields_ok(const struct tm *t)
{
    int year = t->tm_year + 1900; // struct tm counts years from 1900
    return year >= RTC_MIN_YEAR && year <= RTC_MAX_YEAR
        && t->tm_mon >= 0 && t->tm_mon <= 11      // 0 = January
        && t->tm_mday >= 1 && t->tm_mday <= 31
        && t->tm_hour >= 0 && t->tm_hour <= 23
        && t->tm_min >= 0 && t->tm_min <= 59
        && t->tm_sec >= 0 && t->tm_sec <= 59;
}

esp_err_t app_rtc_init(void)
{
    // Configures the 3 GPIOs and reads the clock-halt flag.
    esp_err_t err = ds1302_init(&s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ds1302_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;

    // The DS1302 cannot tell us "I am connected", so we read the time and
    // judge it. Garbage or a halted clock both mean "needs to be set".
    if (app_rtc_is_valid()) {
        struct tm t;
        app_rtc_get_time(&t);
        ESP_LOGI(TAG, "DS1302 time: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        ESP_LOGW(TAG, "DS1302 has no valid time "
                      "(first power-up, flat battery, or module not connected)");
    }
    return ESP_OK;
}

esp_err_t app_rtc_get_time(struct tm *out_time)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return ds1302_get_time(&s_dev, out_time);
}

esp_err_t app_rtc_set_time(const struct tm *time)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!tm_fields_ok(time)) {
        ESP_LOGE(TAG, "refusing to write an out-of-range time");
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Allow writes (the chip ignores writes while write-protect is on).
    esp_err_t err = ds1302_set_write_protect(&s_dev, false);
    // 2. Make sure the oscillator runs. Must come BEFORE set_time, because
    //    the library copies its "halted" flag into the seconds register.
    if (err == ESP_OK) {
        err = ds1302_start(&s_dev, true);
    }
    // 3. Write all date/time registers in one go.
    if (err == ESP_OK) {
        err = ds1302_set_time(&s_dev, time);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "writing time failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DS1302 set to %04d-%02d-%02d %02d:%02d:%02d",
             time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
             time->tm_hour, time->tm_min, time->tm_sec);
    return ESP_OK;
}

bool app_rtc_is_valid(void)
{
    if (!s_ready) {
        return false;
    }

    // Clock-halt flag set = the oscillator is stopped (fresh chip or lost power).
    bool running = false;
    if (ds1302_is_running(&s_dev, &running) != ESP_OK || !running) {
        return false;
    }

    struct tm t;
    if (ds1302_get_time(&s_dev, &t) != ESP_OK) {
        return false;
    }
    return tm_fields_ok(&t);
}