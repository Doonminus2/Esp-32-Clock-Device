/*
 * settings.c — ค่าตั้งของผู้ใช้ เก็บใน NVS (พื้นที่ใน flash ที่ไม่หายตอนดับไฟ)
 *
 * NVS เก็บข้อมูลแบบ key → value แยกเป็นกลุ่มเรียกว่า namespace
 * เราใช้ namespace "clock" และเก็บทุกช่องเป็น uint8_t (bool เก็บเป็น 0/1)
 *
 * เราเก็บสำเนาไว้ใน RAM (s_settings) ด้วย ui อ่านได้ทุกเฟรมโดยไม่ต้องแตะ flash
 * flash เขียนได้จำกัดจำนวนครั้ง จึงเขียนเฉพาะตอนผู้ใช้กดบันทึก
 */
#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "clock"

// ชื่อ key ใน NVS (ยาวได้ไม่เกิน 15 ตัวอักษร)
#define KEY_USE_24H     "use24h"
#define KEY_NIGHT_EN    "night_en"
#define KEY_NIGHT_START "night_s"
#define KEY_NIGHT_END   "night_e"
#define KEY_BRIGHTNESS  "bright"
#define KEY_ALARM_HOUR  "alarm_h"
#define KEY_ALARM_MIN   "alarm_m"
#define KEY_ALARM_EN    "alarm_en"

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

// อ่านค่า 1 ช่อง ถ้ายังไม่เคยบันทึก (key ไม่มี) ค่าใน *field จะเป็นค่า default เดิม
static void read_u8(nvs_handle_t h, const char *key, uint8_t *field)
{
    uint8_t v;
    if (nvs_get_u8(h, key, &v) == ESP_OK) {
        *field = v;
    }
}

static void read_bool(nvs_handle_t h, const char *key, bool *field)
{
    uint8_t v;
    if (nvs_get_u8(h, key, &v) == ESP_OK) {
        *field = (v != 0);
    }
}

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

    s_settings = DEFAULTS;

    nvs_handle_t h;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "first boot: using defaults");  // ยังไม่เคยบันทึกอะไรเลย
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    read_bool(h, KEY_USE_24H, &s_settings.use_24h);
    read_bool(h, KEY_NIGHT_EN, &s_settings.night_mode_enabled);
    read_u8(h, KEY_NIGHT_START, &s_settings.night_start_hour);
    read_u8(h, KEY_NIGHT_END, &s_settings.night_end_hour);
    read_u8(h, KEY_BRIGHTNESS, &s_settings.brightness);
    read_u8(h, KEY_ALARM_HOUR, &s_settings.alarm_hour);
    read_u8(h, KEY_ALARM_MIN, &s_settings.alarm_minute);
    read_bool(h, KEY_ALARM_EN, &s_settings.alarm_enabled);
    nvs_close(h);

    ESP_LOGI(TAG, "loaded: alarm %02d:%02d %s, brightness %d",
             s_settings.alarm_hour, s_settings.alarm_minute,
             s_settings.alarm_enabled ? "ON" : "off", s_settings.brightness);
    return ESP_OK;
}

void settings_get(settings_t *out)
{
    *out = s_settings;
}

esp_err_t settings_set(const settings_t *s)
{
    // 1. ตรวจช่วงค่า ไม่ให้ค่าแปลกๆ ลงไปอยู่ใน flash
    if (s->night_start_hour > 23 || s->night_end_hour > 23 ||
        s->brightness < 1 || s->brightness > 8 ||
        s->alarm_hour > 23 || s->alarm_minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    // 2. เขียนทุกช่อง หยุดทันทีที่เจอ error ตัวแรก
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_USE_24H, s->use_24h);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_NIGHT_EN, s->night_mode_enabled);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_NIGHT_START, s->night_start_hour);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_NIGHT_END, s->night_end_hour);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_BRIGHTNESS, s->brightness);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_ALARM_HOUR, s->alarm_hour);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_ALARM_MIN, s->alarm_minute);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_ALARM_EN, s->alarm_enabled);
    if (err == ESP_OK) err = nvs_commit(h);   // commit = เขียนลง flash จริง
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return err;
    }

    // 3. อัปเดตสำเนาใน RAM
    s_settings = *s;
    return ESP_OK;
}