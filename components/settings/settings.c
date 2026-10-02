#include "settings.h"

esp_err_t settings_init(void)
{
    // TODO(me):
    // 1. nvs_flash_init() (handle ESP_ERR_NVS_NO_FREE_PAGES /
    //    ESP_ERR_NVS_NEW_VERSION_FOUND by erasing and retrying).
    // 2. nvs_open() a namespace for this app's settings.
    // 3. Read each field with a default fallback if the key is missing:
    //    use_24h=true, night_mode_enabled=false, night_start_hour=22,
    //    night_end_hour=6, brightness=4, alarm_hour=7, alarm_minute=0,
    //    alarm_enabled=false (defaults are a starting point - adjust as
    //    wanted).
    // 4. Cache the loaded values for settings_get() to return.
    return ESP_OK;
}

void settings_get(settings_t *out)
{
    // TODO(me): copy the cached settings_t into *out.
}

esp_err_t settings_set(const settings_t *settings)
{
    // TODO(me):
    // 1. Validate ranges (brightness 1-8, hours 0-23, minutes 0-59, etc.).
    // 2. nvs_set_* each field, then nvs_commit().
    // 3. Update the in-memory cache so settings_get() reflects the change.
    return ESP_OK;
}
