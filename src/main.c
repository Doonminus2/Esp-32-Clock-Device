/*
 * main.c — ลำดับการเปิดเครื่องของนาฬิกา LED ESP32
 *
 * ================= ระดับของ log ใน ESP-IDF =================
 * ข้อความจะไปขึ้นที่ Serial Monitor (pio device monitor)
 *
 *   ESP_LOGE(TAG, ...)  E  Error    สีแดง     พังจริง ฟีเจอร์นั้นใช้งานไม่ได้
 *   ESP_LOGW(TAG, ...)  W  Warning  สีเหลือง  ผิดปกติ แต่โปรแกรมยังทำงานต่อได้
 *   ESP_LOGI(TAG, ...)  I  Info     สีเขียว   รายงานความคืบหน้าตามปกติ
 *   ESP_LOGD(TAG, ...)  D  Debug    ไม่มีสี   รายละเอียดตอนหาบั๊ก (ปิดอยู่โดยค่าเริ่มต้น)
 *   ESP_LOGV(TAG, ...)  V  Verbose  ไม่มีสี   ละเอียดที่สุด (ปิดอยู่โดยค่าเริ่มต้น)
 *
 * หน้าตาของแต่ละบรรทัด:   I (1234) main: ข้อความ
 *                        │   │     │
 *                        │   │     └─ TAG บอกว่าข้อความมาจากไฟล์/โมดูลไหน
 *                        │   └─ เวลาเป็นมิลลิวินาที นับจากเปิดเครื่อง
 *                        └─ ตัวอักษรบอกระดับ (E/W/I/D/V)
 *
 * ข้อควรระวัง: ESP_LOGx เป็น macro ถ้าลดระดับ log ลง compiler จะลบทั้งบรรทัดทิ้ง
 * ดังนั้นห้ามเรียกฟังก์ชันที่ต้องทำงานจริง (เช่น xxx_init) ไว้ข้างใน ESP_LOGx(...)
 * ให้เรียกเก็บผลไว้ในตัวแปรก่อน แล้วค่อยเอาตัวแปรไป log
 */

#include "esp_err.h"
#include "esp_log.h"

#include "app_rtc.h"
#include "buzzer.h"
#include "display.h"
#include "input.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"
#include "ui.h"

// TAG จะขึ้นหน้าทุกข้อความ log ของไฟล์นี้
// static = ใช้ได้เฉพาะในไฟล์นี้ ไฟล์อื่นตั้งชื่อ TAG ซ้ำได้โดยไม่ชนกัน
static const char *TAG = "main";

// นับจำนวนโมดูลที่ init ไม่ผ่าน เพื่อสรุปตอนท้าย
static int s_failed_modules = 0;

/*
 * รายงานผล init ของโมดูลที่ "พังได้ นาฬิกายังเดินต่อ"
 *   name    ชื่อโมดูล
 *   role    โมดูลนี้ทำหน้าที่อะไร
 *   if_fail ถ้าพัง นาฬิกาจะเป็นยังไง
 *   err     ค่าที่ xxx_init() คืนมา (ESP_OK = สำเร็จ)
 *
 * %-12s ใน printf = พิมพ์ข้อความ แล้วเติมช่องว่างให้ครบ 12 ตัวอักษร
 * ข้อความจะได้ตั้งตรงกันเป็นคอลัมน์ อ่านง่าย
 */
static void report_optional(const char *name, const char *role,
                            const char *if_fail, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[ OK ] %-12s %s", name, role);                       // เขียว
    } else {
        s_failed_modules++;
        ESP_LOGW(TAG, "[FAIL] %-12s %s -> %s", name, role,
                 esp_err_to_name(err));                                     // เหลือง
        ESP_LOGW(TAG, "       %-12s impact: %s", "", if_fail);              // เหลือง
    }
}

/*
 * รายงานผล init ของโมดูลที่ "ขาดไม่ได้"
 * ถ้าพัง: พิมพ์สีแดง แล้ว ESP_ERROR_CHECK จะ reboot บอร์ด
 */
static void report_critical(const char *name, const char *role, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[ OK ] %-12s %s", name, role);                       // เขียว
    } else {
        ESP_LOGE(TAG, "[FAIL] %-12s %s -> %s", name, role,
                 esp_err_to_name(err));                                     // แดง
        ESP_LOGE(TAG, "       %-12s the clock cannot run without this, rebooting", "");
    }
    // ESP_ERROR_CHECK: ถ้า err ไม่ใช่ ESP_OK จะพิมพ์ชื่อไฟล์+บรรทัดที่พัง แล้ว reboot
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "========== LED matrix clock: booting ==========");

    // esp_err_t = ชนิดข้อมูลของรหัสผลลัพธ์ ฟังก์ชันของ ESP-IDF เกือบทุกตัวคืนค่านี้
    esp_err_t err;

    // ---------- กลุ่มที่ 1: ขาดไม่ได้ (พัง = reboot) ----------

    // ต้องเป็นตัวแรก เพราะเปิด NVS ซึ่ง net ก็ใช้ และ display ต้องอ่านค่าความสว่าง
    err = settings_init();
    report_critical("settings", "user settings in NVS flash", err);

    err = display_init();
    report_critical("display", "LED matrix 32x8 over SPI3", err);

    // ---------- กลุ่มที่ 2: พังได้ นาฬิกายังแสดงเวลาต่อ ----------

    err = app_rtc_init();
    report_optional("app_rtc", "DS1302 real-time clock",
                    "time is lost on power-off, WiFi time only", err);

    err = timekeeping_init();
    report_optional("timekeeping", "system clock + Thai time zone (ICT-7)",
                    "time may be wrong until WiFi sync", err);

    err = input_init();
    report_optional("input", "touch T1/T2 + push button",
                    "buttons do nothing, clock still shows time", err);

    err = buzzer_init();
    report_optional("buzzer", "passive buzzer (beeps, alarm)",
                    "no sound, alarm will be silent", err);

    err = net_init();
    report_optional("net", "WiFi + NTP time sync",
                    "offline mode, using RTC time only", err);

    // ---------- กลุ่มที่ 3: UI ต้องมาท้ายสุด เพราะใช้ทุกโมดูลข้างบน ----------

    err = ui_init();
    report_critical("ui", "screens + state machine (own task)", err);

    // ---------- สรุป ----------
    if (s_failed_modules == 0) {
        ESP_LOGI(TAG, "========== boot complete: all modules OK ==========");
    } else {
        ESP_LOGW(TAG, "========== boot complete: %d module(s) failed ==========",
                 s_failed_modules);
    }

    // app_main จบตรงนี้ แต่บอร์ดไม่ดับ task ของ ui ยังทำงานต่อเบื้องหลัง
}