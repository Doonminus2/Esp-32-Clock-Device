#include "timekeeping.h"

#include <stdlib.h>
#include <sys/time.h>

#include "app_rtc.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "timekeeping";

// POSIX time zone string for Thailand: name "ICT", UTC+7, no daylight saving.
// (The sign is reversed in POSIX strings: "-7" means 7 hours AHEAD of UTC.)
#define TZ_THAILAND "ICT-7"

static bool s_valid = false; // true once the system clock holds a real time

// Design decision: the DS1302 stores LOCAL Thai time (what a human reads),
// while the ESP32 system clock counts UTC seconds. mktime() and
// localtime_r() convert between the two using the TZ setting.

// Sets the ESP32 system clock from a local-time struct tm.
static void set_system_clock(const struct tm *local)
{
    struct tm copy = *local;
    copy.tm_isdst = 0;                  // Thailand has no daylight saving
    time_t epoch = mktime(&copy);       // local time -> seconds since 1970 (UTC)
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

// Waits until the RTC seconds change, then returns that fresh time.
// Copying the time right at the start of a new second keeps the system
// clock in step with the RTC (otherwise it can be up to 1 s off).
static esp_err_t read_rtc_on_second_edge(struct tm *out)
{
    struct tm first;
    esp_err_t err = app_rtc_get_time(&first);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < 120; i++) {           // give up after ~1.2 s
        vTaskDelay(pdMS_TO_TICKS(10));
        err = app_rtc_get_time(out);
        if (err != ESP_OK) {
            return err;
        }
        if (out->tm_sec != first.tm_sec) {
            return ESP_OK;                    // a new second just started
        }
    }
    return ESP_ERR_TIMEOUT;                   // RTC is not ticking
}



esp_err_t timekeeping_init(void)
{
    setenv("TZ", TZ_THAILAND, 1);
    tzset();

    if (!app_rtc_is_valid()) {
        ESP_LOGW(TAG, "no valid time yet: waiting for WiFi sync or manual set");
        s_valid = false;
        return ESP_OK; // not an error: the clock just needs to be set later
    }

    struct tm rtc_time;
        esp_err_t err = read_rtc_on_second_edge(&rtc_time);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reading RTC failed: %s", esp_err_to_name(err));
        return err;
    }

    set_system_clock(&rtc_time);
    s_valid = true;

    struct tm now;
    timekeeping_now(&now);
    ESP_LOGI(TAG, "system clock set from RTC: %04d-%02d-%02d %02d:%02d:%02d (%s)",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
             now.tm_hour, now.tm_min, now.tm_sec, TZ_THAILAND);
    return ESP_OK;
}

bool timekeeping_is_valid(void)
{
    return s_valid;
}

void timekeeping_now(struct tm *out_time)
{
    time_t now;
    time(&now);                  // seconds since 1970 (UTC) from the system clock
    localtime_r(&now, out_time); // -> Thai local date/time, also fills tm_wday
}

esp_err_t timekeeping_set_time(const struct tm *time)
{
    // Normalise first: mktime() fixes tm_wday (day of week) and any
    // overflowing fields, so the RTC gets a fully consistent date.
    struct tm local = *time;
    local.tm_isdst = 0;
    mktime(&local);

    set_system_clock(&local);
    s_valid = true;

    // Persist to the RTC so the time survives a power cut.
    esp_err_t err = app_rtc_set_time(&local);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "system clock updated, but saving to RTC failed: %s",
                 esp_err_to_name(err));
    }
    return err;
}