/*
 * settings.c — ตอนนี้ทำแค่ส่วนที่ต้องมีก่อน: เปิด NVS + ค่าเริ่มต้นในหน่วยความจำ
 * (WiFi เก็บชื่อ/รหัสผ่านไว้ใน NVS จึงต้อง nvs_flash_init() ก่อน net_init())
 * การอ่าน/บันทึกค่าลง NVS จริงๆ เป็นขั้นถัดไปตอนทำเมนู 12H/NITE/BRT
 */
#include "settings.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

static const settings_t DEFAULTS = {
    .use_24h = true,
    .night_mode_enabled = false,
    .night_start_hour = 22,
    .night_end_hour = 6,
    .brightness = 4,
    .alarm_hour = 7,
    .alarm_minute = 0,
    .alarm_enabled = false,
};

static settings_t s_settings;

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    // พาร์ทิชัน NVS เต็ม หรือถูกฟอร์แมตด้วย IDF เวอร์ชันอื่น → ล้างแล้วเริ่มใหม่
    // (ข้อมูลที่หายรวมถึงชื่อ/รหัส WiFi ที่เคยตั้งไว้)
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), erasing", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        return err;
    }

    s_settings = DEFAULTS; // TODO(me): โหลดค่าที่บันทึกไว้จาก NVS ทับ (ขั้นถัดไป)
    return ESP_OK;
}

void settings_get(settings_t *out)
{
    *out = s_settings;
}

esp_err_t settings_set(const settings_t *settings)
{
    // TODO(me): ตรวจช่วงค่า, nvs_set_* ทีละช่อง, nvs_commit(), อัปเดต s_settings
    return ESP_ERR_NOT_SUPPORTED;
}