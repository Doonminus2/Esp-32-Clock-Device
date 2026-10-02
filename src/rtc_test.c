#include "rtc_test.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_rtc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timekeeping.h"

static const char *TAG = "rtc_test";

// 1 = always overwrite the RTC with the build time (use once if the time is wrong).
// 0 = only set it when the RTC has no valid time (normal).
#define RTC_TEST_FORCE_SET 0

// Builds a struct tm from the moment this file was compiled.
// __DATE__ looks like "Oct  3 2026", __TIME__ like "14:05:33".
static void build_time(struct tm *out)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;

    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    memset(out, 0, sizeof(*out));
    out->tm_year = year - 1900;
    out->tm_mon = (int)((strstr(months, mon) - months) / 3);
    out->tm_mday = day;
    out->tm_hour = hh;
    out->tm_min = mm;
    out->tm_sec = ss;
    mktime(out); // fills in tm_wday (day of week)
}

static void log_tm(const char *label, const struct tm *t)
{
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    ESP_LOGI(TAG, "%-8s %s %04d-%02d-%02d %02d:%02d:%02d", label,
             (t->tm_wday >= 0 && t->tm_wday <= 6) ? days[t->tm_wday] : "???",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
}

void rtc_smoke_test(void)
{
    // Step 1: did the RTC keep a valid time while the board was off?
    bool valid = app_rtc_is_valid();
    ESP_LOGI(TAG, "step 1/3: RTC valid at boot: %s", valid ? "YES" : "NO");

    // Step 2: set the time if needed (from the build time of this file).
    if (!valid || RTC_TEST_FORCE_SET) {
        struct tm t;
        build_time(&t);
        log_tm("set to", &t);
        ESP_LOGI(TAG, "step 2/3: setting RTC to the build time (may be ~1 min behind)");
        ESP_ERROR_CHECK(timekeeping_set_time(&t));
    } else {
        ESP_LOGI(TAG, "step 2/3: RTC already valid, not touching it");
    }

    // Step 3: print RTC and system time every second. Both must tick together.
    ESP_LOGI(TAG, "step 3/3: RTC vs system clock, every second (runs forever)");
    int last_sec = -1;
    int stuck = 0;
    while (1) {
        struct tm rtc, sys;
        if (app_rtc_get_time(&rtc) != ESP_OK) {
            ESP_LOGE(TAG, "app_rtc_get_time failed");
        }
        timekeeping_now(&sys);
        log_tm("rtc", &rtc);
        log_tm("system", &sys);

        // If the RTC seconds stop changing, its crystal is not running.
        stuck = (rtc.tm_sec == last_sec) ? stuck + 1 : 0;
        last_sec = rtc.tm_sec;
        if (stuck >= 3) {
            ESP_LOGW(TAG, "RTC seconds are not moving: check the crystal / wiring");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}