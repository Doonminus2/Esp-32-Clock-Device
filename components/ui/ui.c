/*
 * ui.c — UI FSM (DESIGN.md 8.1) รอบละ 50 ms
 *
 * ทุกรอบทำ 3 อย่างตามลำดับ:
 *   1. หยิบ event ปุ่ม/ทัชทั้งหมดในคิวมาจัดการ   handle_input()
 *   2. เปลี่ยน state ตามเวลา/สถานะภายนอก        update_state()
 *   3. วาดจอใหม่ทั้งเฟรมแล้วส่งทีเดียว          draw() + display_flush()
 *
 * รอบนี้มี: หน้านาฬิกา, ป้าย ONLINE/OFFLINE, เมนูตั้งเวลา/วันที่, หน้า SETUP (WiFi)
 * ยังไม่มี: โหมดวันที่/จับเวลา/ปลุก, ตัวเลขม้วน, เมนู 12H/NITE/BRT
 */
#include "ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "buzzer.h"
#include "display.h"
#include "display_font.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "net.h"
#include "timekeeping.h"

static const char *TAG = "ui";

// ---------------------------------------------------------------------------
// ค่าตั้ง
// ---------------------------------------------------------------------------

#define UI_TICK_MS           50
#define UI_BLINK_MS          500
#define UI_NET_STATUS_MS     2000    // ป้าย ONLINE/OFFLINE ค้างไว้ 2 วิ
#define UI_MENU_TIMEOUT_MS   30000   // ไม่แตะอะไร 30 วิ → ออกจากเมนู ไม่บันทึก
#define UI_TASK_STACK        4096
#define UI_TASK_PRIO         5

#define UI_TEXT_Y            1       // ตัวอักษรสูง 5 แถว วางที่ y=1 จะอยู่กลางจอ 8 แถว
#define UI_DEFAULT_YEAR      2026    // วันที่ตั้งต้น ถ้ายังไม่เคยมีเวลาเลย

// ---------------------------------------------------------------------------
// เมนู
// ---------------------------------------------------------------------------

typedef enum {
    MENU_TIME,
    MENU_DATE,
    MENU_EXIT,
    MENU_COUNT,      // ตัวสุดท้าย = จำนวนรายการ (เทคนิคที่ใช้กันบ่อยใน C)
} menu_item_t;

static const char *const MENU_LABELS[MENU_COUNT] = { "TIME", "DATE", "EXIT" };

// ช่องหนึ่งช่องที่กำลังแก้ เช่น ชั่วโมง (0–23)
typedef struct {
    int value;
    int min;
    int max;
} edit_field_t;

#define MAX_FIELDS 3

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

static ui_state_t s_state = UI_STATE_BOOT;
static display_framebuffer_t s_fb;
static QueueHandle_t s_input_q = NULL;

static uint32_t s_state_since_ms = 0;   // เวลาที่เข้า state ปัจจุบัน
static uint32_t s_last_input_ms = 0;    // เวลาที่แตะ/กดล่าสุด

static int s_menu_index = 0;            // รายการเมนูที่เลือกอยู่
static bool s_editing = false;          // false = เลือกรายการ, true = กำลังแก้ค่า
static menu_item_t s_edit_item;
static edit_field_t s_fields[MAX_FIELDS];
static int s_field_count = 0;
static int s_field_index = 0;           // ช่องที่กำลังกะพริบ
static bool s_then_set_date = false;    // ตั้งเวลาเสร็จแล้วให้ไปตั้งวันที่ต่อ (ตอนยังไม่มีเวลาเลย)
static bool s_auto_menu_done = false;   // เปิดเมนูตั้งเวลาอัตโนมัติไปแล้วหนึ่งครั้ง

static const char *const STATE_NAMES[] = {
    "BOOT", "WAIT_TIME", "MODE_CLOCK", "MODE_DATE", "MODE_STOPWATCH", "MODE_ALARM",
    "SHOW_NET_STATUS", "MENU", "ALARM_RINGING", "PROVISIONING",
};

// มิลลิวินาทีนับจากเปิดเครื่อง
static uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void set_state(ui_state_t next)
{
    if (next != s_state) {
        ESP_LOGI(TAG, "%s -> %s", STATE_NAMES[s_state], STATE_NAMES[next]);
        s_state = next;
        s_state_since_ms = now_ms();
    }
}

// ช่วงกะพริบ: แสดงทันทีหลังกดปุ่ม (ผู้ใช้จะเห็นค่าที่เพิ่งเปลี่ยน) แล้วค่อยกะพริบ
static bool blink_visible(void)
{
    return ((now_ms() - s_last_input_ms) / UI_BLINK_MS) % 2 == 0;
}

// ---------------------------------------------------------------------------
// วาด (layout ก, DESIGN.md 5.1)
// ฟังก์ชันวาดทุกตัวไม่ล้างจอเอง draw() เป็นคนล้างครั้งเดียวต้นเฟรม
// ---------------------------------------------------------------------------

static void draw_big(int x, int digit)
{
    display_draw_glyph(&s_fb, x, 0, font_big_digits[digit], FONT_BIG_W, FONT_BIG_H);
}

static void draw_small(int x, int digit)
{
    display_draw_glyph(&s_fb, x, 2, font_small_digits[digit], FONT_SMALL_W, FONT_SMALL_H);
}

static void draw_big_colon(void)
{
    display_draw_pixel(&s_fb, 10, 2, true);
    display_draw_pixel(&s_fb, 10, 4, true);
}

// HH:MM ตัวใหญ่ ซ่อนชั่วโมง/นาทีได้ (ใช้ทำกะพริบตอนตั้งเวลา)
static void draw_hhmm(int hour, int min, bool show_hour, bool show_min)
{
    if (show_hour) {
        draw_big(0, hour / 10);
        draw_big(5, hour % 10);
    }
    if (show_min) {
        draw_big(12, min / 10);
        draw_big(17, min % 10);
    }
    draw_big_colon();
}

static void draw_clock(const struct tm *t)
{
    draw_hhmm(t->tm_hour, t->tm_min, true, true);
    draw_small(24, t->tm_sec / 10);
    draw_small(28, t->tm_sec % 10);
    display_draw_pixel(&s_fb, 22, 3, true);    // โคลอนเล็กหน้าวินาที
    display_draw_pixel(&s_fb, 22, 5, true);

    // จุดสถานะคอลัมน์ 31 แถว 7: ติด = ยังไม่ได้เวลาจากอินเทอร์เน็ต
    if (net_get_state() != NET_STATE_SYNCED) {
        display_draw_pixel(&s_fb, 31, 7, true);
    }
}

static void draw_no_time(void)
{
    if (!((now_ms() / UI_BLINK_MS) % 2 == 0)) {
        return;
    }
    const int dash_x[] = {0, 5, 12, 17};
    for (int i = 0; i < 4; i++) {
        for (int dx = 0; dx < FONT_BIG_W; dx++) {
            display_draw_pixel(&s_fb, dash_x[i] + dx, 3, true);
        }
    }
    draw_big_colon();
}

// ข้อความฟอนต์ 3×5 แต่ละตัวกว้าง 3 + ช่องว่าง 1
static int text_width(const char *s)
{
    int n = strlen(s);
    return n == 0 ? 0 : n * (FONT_SMALL_W + 1) - 1;
}

static void draw_text(int x, int y, const char *s)
{
    for (; *s != '\0'; s++) {
        const uint8_t *glyph = font_small_glyph(*s);
        if (glyph != NULL) {
            display_draw_glyph(&s_fb, x, y, glyph, FONT_SMALL_W, FONT_SMALL_H);
        }
        x += FONT_SMALL_W + 1;
    }
}

static void draw_text_centered(const char *s)
{
    draw_text((DISPLAY_WIDTH - text_width(s)) / 2, UI_TEXT_Y, s);
}

static void draw_menu(void)
{
    if (!s_editing) {
        draw_text_centered(MENU_LABELS[s_menu_index]);
        return;
    }

    bool show = blink_visible();
    if (s_edit_item == MENU_TIME) {
        draw_hhmm(s_fields[0].value, s_fields[1].value,
                  s_field_index != 0 || show,
                  s_field_index != 1 || show);
    } else {
        // "DD/MM/YY" กว้าง 8×4−1 = 31 จุด พอดีจอ
        char buf[9];
        buf[0] = '0' + s_fields[0].value / 10;
        buf[1] = '0' + s_fields[0].value % 10;
        buf[2] = '/';
        buf[3] = '0' + s_fields[1].value / 10;
        buf[4] = '0' + s_fields[1].value % 10;
        buf[5] = '/';
        buf[6] = '0' + s_fields[2].value / 10;
        buf[7] = '0' + s_fields[2].value % 10;
        buf[8] = '\0';
        if (!show) {
            int pos = s_field_index * 3;   // ช่อง 0,1,2 เริ่มที่ตัวอักษร 0,3,6
            buf[pos] = ' ';
            buf[pos + 1] = ' ';
        }
        draw_text(0, UI_TEXT_Y, buf);
    }
}

static void draw(void)
{
    display_clear(&s_fb);

    switch (s_state) {
    case UI_STATE_MODE_CLOCK: {
        struct tm now;
        timekeeping_now(&now);
        draw_clock(&now);
        break;
    }
    case UI_STATE_WAIT_TIME:
        draw_no_time();
        break;
    case UI_STATE_SHOW_NET_STATUS:
        draw_text_centered(net_get_state() == NET_STATE_SYNCED ? "ONLINE" : "OFFLINE");
        break;
    case UI_STATE_MENU:
        draw_menu();
        break;
    case UI_STATE_PROVISIONING:
        draw_text_centered("SETUP");
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// เมนูตั้งเวลา/วันที่
// ---------------------------------------------------------------------------

static int days_in_month(int month, int year)  // month 1–12
{
    static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return (month == 2 && leap) ? 29 : DAYS[month - 1];
}

// เวลาปัจจุบัน หรือ 1 ม.ค. UI_DEFAULT_YEAR 00:00 ถ้ายังไม่เคยมีเวลา
static void get_base_time(struct tm *t)
{
    if (timekeeping_is_valid()) {
        timekeeping_now(t);
        return;
    }
    memset(t, 0, sizeof(*t));
    t->tm_year = UI_DEFAULT_YEAR - 1900;
    t->tm_mday = 1;
}

static void set_field(int i, int value, int min, int max)
{
    s_fields[i] = (edit_field_t){ .value = value, .min = min, .max = max };
}

static void start_edit(menu_item_t item)
{
    struct tm t;
    get_base_time(&t);

    s_edit_item = item;
    s_editing = true;
    s_field_index = 0;

    if (item == MENU_TIME) {
        set_field(0, t.tm_hour, 0, 23);
        set_field(1, t.tm_min, 0, 59);
        s_field_count = 2;
    } else {
        set_field(0, t.tm_mday, 1, 31);
        set_field(1, t.tm_mon + 1, 1, 12);             // tm_mon นับ 0–11
        set_field(2, t.tm_year + 1900 - 2000, 24, 99); // แสดงปี 2 หลัก (2024–2099)
        s_field_count = 3;
    }
}

// +1 / −1 แบบวนรอบ: 23 +1 → 0, 0 −1 → 23
static void adjust_field(int delta)
{
    edit_field_t *f = &s_fields[s_field_index];
    f->value += delta;
    if (f->value > f->max) {
        f->value = f->min;
    } else if (f->value < f->min) {
        f->value = f->max;
    }
}

static void leave_menu(void)
{
    s_editing = false;
    set_state(timekeeping_is_valid() ? UI_STATE_MODE_CLOCK : UI_STATE_WAIT_TIME);
}

static void open_menu(void)
{
    buzzer_play(BUZZER_PATTERN_MENU_ENTER);
    s_menu_index = 0;
    s_editing = false;
    s_then_set_date = !timekeeping_is_valid();
    s_last_input_ms = now_ms();   // เริ่มนับ timeout 30 วิ ใหม่
    set_state(UI_STATE_MENU);
}

static void save_edit(void)
{
    struct tm t;
    get_base_time(&t);

    if (s_edit_item == MENU_TIME) {
        t.tm_hour = s_fields[0].value;
        t.tm_min = s_fields[1].value;
        t.tm_sec = 0;                                   // DESIGN.md 7: วินาที = 00
    } else {
        int year = 2000 + s_fields[2].value;
        int month = s_fields[1].value;
        int day = s_fields[0].value;
        if (day > days_in_month(month, year)) {         // เช่น 31/02 → 28/02 (หรือ 29)
            day = days_in_month(month, year);
        }
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
    }

    esp_err_t err = timekeeping_set_time(&t);           // ตั้ง system clock + เขียน RTC
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time set, but RTC write failed: %s", esp_err_to_name(err));
    }
    buzzer_play(BUZZER_PATTERN_SAVE);
    ESP_LOGI(TAG, "saved %s", MENU_LABELS[s_edit_item]);

    if (s_edit_item == MENU_TIME && s_then_set_date) {
        s_then_set_date = false;
        start_edit(MENU_DATE);                          // ยังไม่เคยมีวันที่ → ตั้งต่อเลย
        return;
    }
    leave_menu();
}

static void menu_handle_input(input_event_t ev)
{
    if (ev == INPUT_EVENT_HOLD_3S) {
        leave_menu();                                   // ยกเลิก ไม่บันทึก
        return;
    }

    if (!s_editing) {
        // หน้ารายการ: T1/T2 เลื่อน, ปุ่มกด = เลือก
        if (ev == INPUT_EVENT_PREV || ev == INPUT_EVENT_PREV_REPEAT) {
            s_menu_index = (s_menu_index + MENU_COUNT - 1) % MENU_COUNT;
        } else if (ev == INPUT_EVENT_NEXT || ev == INPUT_EVENT_NEXT_REPEAT) {
            s_menu_index = (s_menu_index + 1) % MENU_COUNT;
        } else if (ev == INPUT_EVENT_ENTER) {
            if (s_menu_index == MENU_EXIT) {
                leave_menu();
            } else {
                start_edit((menu_item_t)s_menu_index);
            }
        }
        return;
    }

    // หน้าแก้ค่า: T1 −1, T2 +1 (ค้าง = วิ่งเร็ว), ปุ่มกด = ช่องถัดไป / บันทึก
    if (ev == INPUT_EVENT_PREV || ev == INPUT_EVENT_PREV_REPEAT) {
        adjust_field(-1);
    } else if (ev == INPUT_EVENT_NEXT || ev == INPUT_EVENT_NEXT_REPEAT) {
        adjust_field(+1);
    } else if (ev == INPUT_EVENT_ENTER) {
        s_field_index++;
        if (s_field_index >= s_field_count) {
            save_edit();
        }
    }
}

// ---------------------------------------------------------------------------
// FSM
// ---------------------------------------------------------------------------

static void start_provisioning(void)
{
    buzzer_play(BUZZER_PATTERN_WIFI_ENTER);
    esp_err_t err = net_start_provisioning();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start WiFi setup: %s", esp_err_to_name(err));
        return;
    }
    set_state(UI_STATE_PROVISIONING);
}

static void handle_input(input_event_t ev)
{
    s_last_input_ms = now_ms();

    if (ev == INPUT_EVENT_BOOT_HOLD) {
        start_provisioning();
        return;
    }

    switch (s_state) {
    case UI_STATE_MODE_CLOCK:
        if (ev == INPUT_EVENT_ENTER) {
            set_state(UI_STATE_SHOW_NET_STATUS);
        } else if (ev == INPUT_EVENT_HOLD_3S) {
            open_menu();
        }
        // PREV/NEXT: สลับโหมด วันที่/จับเวลา/ปลุก (รอบถัดไป)
        break;

    case UI_STATE_WAIT_TIME:
        if (ev == INPUT_EVENT_HOLD_3S) {
            open_menu();
        }
        break;

    case UI_STATE_SHOW_NET_STATUS:
        set_state(UI_STATE_MODE_CLOCK);                 // แตะ/กดอะไรก็ได้ = กลับ
        break;

    case UI_STATE_MENU:
        menu_handle_input(ev);
        break;

    default:
        break;                                          // PROVISIONING: ไม่รับปุ่ม
    }
}

// การเปลี่ยน state ที่ไม่ได้มาจากปุ่ม แต่มาจากเวลาหรือสถานะของโมดูลอื่น
static void update_state(void)
{
    uint32_t now = now_ms();

    switch (s_state) {
    case UI_STATE_BOOT:
        set_state(timekeeping_is_valid() ? UI_STATE_MODE_CLOCK : UI_STATE_WAIT_TIME);
        break;

    case UI_STATE_WAIT_TIME:
        if (timekeeping_is_valid()) {
            set_state(UI_STATE_MODE_CLOCK);             // ได้เวลาจาก SNTP แล้ว
        } else if (!s_auto_menu_done && net_get_state() == NET_STATE_OFFLINE) {
            s_auto_menu_done = true;                    // ไม่มีเน็ต → ให้ตั้งเวลาเอง (ครั้งเดียว)
            open_menu();
            start_edit(MENU_TIME);
        }
        break;

    case UI_STATE_SHOW_NET_STATUS:
        if (now - s_state_since_ms >= UI_NET_STATUS_MS) {
            set_state(UI_STATE_MODE_CLOCK);
        }
        break;

    case UI_STATE_MENU:
        if (now - s_last_input_ms >= UI_MENU_TIMEOUT_MS) {
            ESP_LOGI(TAG, "menu timeout, nothing saved");
            leave_menu();
        }
        break;

    case UI_STATE_PROVISIONING:
        if (net_get_state() != NET_STATE_PROVISIONING) {
            set_state(UI_STATE_BOOT);                   // ตั้งค่า WiFi เสร็จ
        }
        break;

    default:
        break;
    }
}

static void ui_task(void *arg)
{
    while (1) {
        input_event_t ev;
        // timeout 0 = ไม่รอ ถ้าคิวว่างก็ไปต่อ
        while (s_input_q != NULL && xQueueReceive(s_input_q, &ev, 0) == pdTRUE) {
            handle_input(ev);
        }

        update_state();
        draw();
        display_flush(&s_fb);

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

esp_err_t ui_init(void)
{
    s_input_q = input_get_event_queue();   // NULL ได้ ถ้า input_init() พัง
    if (s_input_q == NULL) {
        ESP_LOGW(TAG, "no input queue: buttons will be ignored");
    }

    BaseType_t ok = xTaskCreate(ui_task, "ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "could not create ui task (out of memory)");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ui task started, %d ms tick", UI_TICK_MS);
    return ESP_OK;
}

ui_state_t ui_get_state(void)
{
    return s_state;
}