/*
 * ui.c — UI FSM (DESIGN.md 8.1) รอบละ 50 ms
 *
 * ทุกรอบทำ 3 อย่างตามลำดับ:
 *   1. หยิบ event ปุ่ม/ทัชทั้งหมดในคิวมาจัดการ   handle_input()
 *   2. เปลี่ยน state ตามเวลา/สถานะภายนอก        update_state()
 *   3. วาดจอใหม่ทั้งเฟรมแล้วส่งทีเดียว          draw() + display_flush()
 *
 * โหมดหลัก 4 โหมด สลับด้วย T1 ◀ / T2 ▶ (วนรอบ):
 *   นาฬิกา (CLK) → วันที่ (DATE) → จับเวลา (STW) → ปลุก (ALM)
 * ทุกโหมด: กดปุ่มค้าง 3 วิ = เมนู (TIME / DATE / ALRM / EXIT)
 */
#include "ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
#include "settings.h"
#include "timekeeping.h"

static const char *TAG = "ui";

// ---------------------------------------------------------------------------
// ค่าตั้ง
// ---------------------------------------------------------------------------

#define UI_TICK_MS           50
#define UI_BLINK_MS          500
#define UI_LABEL_MS          800     // ป้ายชื่อโหมดค้าง 0.8 วิ (DESIGN.md 5.6)
#define UI_SCROLL_MS         50      // หน้าวันที่เลื่อน 1 จุดทุก 50 ms
#define UI_NET_STATUS_MS     2000    // ป้าย ONLINE/OFFLINE ค้างไว้ 2 วิ
#define UI_MENU_TIMEOUT_MS   30000   // ไม่แตะอะไร 30 วิ → ออกจากเมนู ไม่บันทึก
#define UI_ALARM_RING_MS     60000   // ปลุกดังนานสุด 60 วิ
#define UI_TASK_STACK        4096
#define UI_TASK_PRIO         5

#define UI_TEXT_Y            1       // ตัวอักษรสูง 5 แถว วางที่ y=1 จะอยู่กลางจอ 8 แถว
#define UI_SMALL_Y           2       // ตัวเล็กในช่องวินาที ชิดล่างเท่ากับตัวใหญ่
#define UI_DEFAULT_YEAR      2026    // วันที่ตั้งต้น ถ้ายังไม่เคยมีเวลาเลย

// ---------------------------------------------------------------------------
// โหมดหลัก
// ---------------------------------------------------------------------------

static const ui_state_t MODES[] = {
    UI_STATE_MODE_CLOCK, UI_STATE_MODE_DATE, UI_STATE_MODE_STOPWATCH, UI_STATE_MODE_ALARM,
};
static const char *const MODE_LABELS[] = { "CLK", "DATE", "STW", "ALM" };
#define MODE_COUNT 4

static const char *const DAY_NAMES[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// ---------------------------------------------------------------------------
// เมนู
// ---------------------------------------------------------------------------

typedef enum {
    MENU_TIME,
    MENU_DATE,
    MENU_ALARM,
    MENU_EXIT,
    MENU_COUNT,      // ตัวสุดท้าย = จำนวนรายการ (เทคนิคที่ใช้กันบ่อยใน C)
} menu_item_t;

static const char *const MENU_LABELS[MENU_COUNT] = { "TIME", "DATE", "ALRM", "EXIT" };

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

// โหมดหลัก
static int s_mode_index = 0;            // ช่องใน MODES[] ที่อยู่ตอนนี้
static uint32_t s_label_until_ms = 0;   // แสดงป้ายชื่อโหมดถึงเวลานี้

// จับเวลา: เวลาที่สะสมไว้ + (ถ้ากำลังเดิน) เวลาตั้งแต่กดเริ่ม
static bool s_sw_running = false;
static uint32_t s_sw_start_ms = 0;
static uint32_t s_sw_acc_ms = 0;

// ปลุก
static ui_state_t s_ring_return = UI_STATE_MODE_CLOCK; // ปลุกจบแล้วกลับไป state ไหน
static int s_last_alarm_key = -1;       // นาทีที่ปลุกไปแล้ว กันปลุกซ้ำในนาทีเดียวกัน

// เมนู
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
    display_draw_glyph(&s_fb, x, UI_SMALL_Y, font_small_digits[digit], FONT_SMALL_W, FONT_SMALL_H);
}

static void draw_big_colon(void)
{
    display_draw_pixel(&s_fb, 10, 2, true);
    display_draw_pixel(&s_fb, 10, 4, true);
}

// ตัวเลข 2 คู่ตัวใหญ่ "AA:BB" ซ่อนคู่ซ้าย/ขวาได้ (ใช้ทำกะพริบตอนตั้งค่า)
// ใช้ได้ทั้ง HH:MM (นาฬิกา) และ MM:SS (จับเวลา)
static void draw_big_pair(int left, int right, bool show_left, bool show_right)
{
    if (show_left) {
        draw_big(0, left / 10);
        draw_big(5, left % 10);
    }
    if (show_right) {
        draw_big(12, right / 10);
        draw_big(17, right % 10);
    }
    draw_big_colon();
}

// ตัวเลข 2 หลักตัวเล็กในช่องวินาที (คอลัมน์ 22–30)
static void draw_small_pair(int value)
{
    display_draw_pixel(&s_fb, 22, 3, true);    // โคลอนเล็ก
    display_draw_pixel(&s_fb, 22, 5, true);
    draw_small(24, value / 10);
    draw_small(28, value % 10);
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

// ---- หน้าต่างๆ ----

static void draw_clock(void)
{
    struct tm t;
    timekeeping_now(&t);
    draw_big_pair(t.tm_hour, t.tm_min, true, true);
    draw_small_pair(t.tm_sec);

    settings_t st;
    settings_get(&st);
    // คอลัมน์ 31: แถว 0 = เปิดปลุกอยู่, แถว 7 = ยังไม่ได้เวลาจากอินเทอร์เน็ต
    if (st.alarm_enabled) {
        display_draw_pixel(&s_fb, 31, 0, true);
    }
    if (net_get_state() != NET_STATE_SYNCED) {
        display_draw_pixel(&s_fb, 31, 7, true);
    }
}

// "FRI 3/10/26" กว้างเกินจอ → เลื่อนจากขวาไปซ้ายวนไปเรื่อยๆ (DESIGN.md 5.3)
static void draw_date(uint32_t shown_ms)
{
    struct tm t;
    timekeeping_now(&t);

    char buf[32];   // เผื่อพอสำหรับทุกค่าที่ int เป็นได้ (คอมไพเลอร์เช็กให้)
    snprintf(buf, sizeof(buf), "%s %d/%d/%02d", DAY_NAMES[t.tm_wday],
             t.tm_mday, t.tm_mon + 1, (t.tm_year + 1900) % 100);

    // ข้อความเริ่มจากขอบขวา (x=32) เลื่อนออกไปทางซ้ายจนหายหมด แล้ววนใหม่
    int period = text_width(buf) + DISPLAY_WIDTH;
    int step = (shown_ms / UI_SCROLL_MS) % period;
    draw_text(DISPLAY_WIDTH - step, UI_TEXT_Y, buf);
}

static uint32_t stopwatch_elapsed_ms(void)
{
    return s_sw_acc_ms + (s_sw_running ? now_ms() - s_sw_start_ms : 0);
}

// ไม่ถึง 1 ชม.: MM:SS ตัวใหญ่ + เศษวินาที (1/100) ตัวเล็ก
// เกิน 1 ชม.:   HH:MM ตัวใหญ่ + วินาที ตัวเล็ก (DESIGN.md 5.4)
static void draw_stopwatch(void)
{
    uint32_t ms = stopwatch_elapsed_ms();
    uint32_t sec = ms / 1000;

    if (sec < 3600) {
        draw_big_pair(sec / 60, sec % 60, true, true);
        draw_small_pair((ms / 10) % 100);
    } else {
        uint32_t hours = sec / 3600;
        draw_big_pair(hours > 99 ? 99 : hours, (sec / 60) % 60, true, true);
        draw_small_pair(sec % 60);
    }
}

// เวลาปลุก HH:MM ตัวใหญ่ + "ON" หรือ "--" ในช่องวินาที (DESIGN.md 5.5)
static void draw_alarm(void)
{
    settings_t st;
    settings_get(&st);
    draw_big_pair(st.alarm_hour, st.alarm_minute, true, true);
    draw_text(24, UI_SMALL_Y, st.alarm_enabled ? "ON" : "--");
}

static void draw_ringing(void)
{
    // กะพริบเร็ว 4 ครั้ง/วินาที ให้เห็นชัดว่ากำลังปลุก
    if ((now_ms() / 250) % 2 == 0) {
        settings_t st;
        settings_get(&st);
        draw_big_pair(st.alarm_hour, st.alarm_minute, true, true);
        draw_text(24, UI_SMALL_Y, "AL");
    }
}

static void draw_no_time(void)
{
    if ((now_ms() / UI_BLINK_MS) % 2 != 0) {
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

static void draw_menu(void)
{
    if (!s_editing) {
        draw_text_centered(MENU_LABELS[s_menu_index]);
        return;
    }

    bool show = blink_visible();
    if (s_edit_item == MENU_TIME || s_edit_item == MENU_ALARM) {
        draw_big_pair(s_fields[0].value, s_fields[1].value,
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
    uint32_t now = now_ms();

    switch (s_state) {
    case UI_STATE_MODE_CLOCK:
    case UI_STATE_MODE_DATE:
    case UI_STATE_MODE_STOPWATCH:
    case UI_STATE_MODE_ALARM:
        // ช่วงแรกหลังสลับโหมด: แสดงป้ายชื่อโหมดก่อน
        // (int32_t)(a − b) < 0 อ่านว่า "a ยังมาไม่ถึง b" ปลอดภัยแม้ตัวนับวนรอบ
        if ((int32_t)(now - s_label_until_ms) < 0) {
            draw_text_centered(MODE_LABELS[s_mode_index]);
        } else if (s_state == UI_STATE_MODE_CLOCK) {
            draw_clock();
        } else if (s_state == UI_STATE_MODE_DATE) {
            draw_date(now - s_label_until_ms);
        } else if (s_state == UI_STATE_MODE_STOPWATCH) {
            draw_stopwatch();
        } else {
            draw_alarm();
        }
        break;
    case UI_STATE_WAIT_TIME:
        draw_no_time();
        break;
    case UI_STATE_SHOW_NET_STATUS:
        draw_text_centered(net_get_state() == NET_STATE_SYNCED ? "ONLINE" : "OFFLINE");
        break;
    case UI_STATE_MENU:
        draw_menu();
        break;
    case UI_STATE_ALARM_RINGING:
        draw_ringing();
        break;
    case UI_STATE_PROVISIONING:
        draw_text_centered("SETUP");
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// โหมดหลัก
// ---------------------------------------------------------------------------

static void switch_mode(int delta)
{
    s_mode_index = (s_mode_index + delta + MODE_COUNT) % MODE_COUNT;
    s_label_until_ms = now_ms() + UI_LABEL_MS;
    set_state(MODES[s_mode_index]);
}

// ปุ่มกดในหน้าจับเวลา วน 3 จังหวะ: เริ่ม → หยุด → รีเซ็ตเป็น 0
static void stopwatch_enter(void)
{
    if (s_sw_running) {
        s_sw_acc_ms = stopwatch_elapsed_ms();   // หยุด: เก็บเวลาที่เดินไปแล้ว
        s_sw_running = false;
        ESP_LOGI(TAG, "stopwatch stop at %lu ms", (unsigned long)s_sw_acc_ms);
    } else if (s_sw_acc_ms > 0) {
        s_sw_acc_ms = 0;                        // หยุดอยู่และมีค่า → รีเซ็ต
        ESP_LOGI(TAG, "stopwatch reset");
    } else {
        s_sw_start_ms = now_ms();               // เป็น 0 อยู่ → เริ่ม
        s_sw_running = true;
        ESP_LOGI(TAG, "stopwatch start");
    }
}

// ปุ่มกดในหน้าปลุก: เปิด/ปิดปลุก แล้วบันทึกลง NVS
static void alarm_toggle(void)
{
    settings_t st;
    settings_get(&st);
    st.alarm_enabled = !st.alarm_enabled;
    if (settings_set(&st) == ESP_OK) {
        buzzer_play(BUZZER_PATTERN_SAVE);
        ESP_LOGI(TAG, "alarm %s", st.alarm_enabled ? "ON" : "off");
    }
}

// ---------------------------------------------------------------------------
// ปลุก
// ---------------------------------------------------------------------------

static void start_ringing(void)
{
    s_ring_return = s_state;
    buzzer_play(BUZZER_PATTERN_ALARM);
    set_state(UI_STATE_ALARM_RINGING);
}

static void stop_ringing(void)
{
    buzzer_stop();
    s_last_input_ms = now_ms();       // ถ้ากลับไปเมนู จะได้ไม่ timeout ทันที
    set_state(s_ring_return);
}

// เรียกทุกรอบ: ถึงเวลาปลุกหรือยัง
static void check_alarm(void)
{
    // ไม่ปลุกตอนยังไม่มีเวลา, ตอนตั้ง WiFi, หรือตอนกำลังปลุกอยู่แล้ว
    if (s_state == UI_STATE_BOOT || s_state == UI_STATE_WAIT_TIME ||
        s_state == UI_STATE_PROVISIONING || s_state == UI_STATE_ALARM_RINGING) {
        return;
    }

    settings_t st;
    settings_get(&st);
    if (!st.alarm_enabled) {
        return;
    }

    struct tm t;
    timekeeping_now(&t);
    if (t.tm_hour != st.alarm_hour || t.tm_min != st.alarm_minute) {
        return;
    }

    // เลขประจำนาทีนี้ของปี: ปลุกนาทีละครั้ง กดหยุดแล้วไม่ดังซ้ำในนาทีเดียวกัน
    int key = t.tm_yday * 24 * 60 + t.tm_hour * 60 + t.tm_min;
    if (key == s_last_alarm_key) {
        return;
    }
    s_last_alarm_key = key;
    ESP_LOGI(TAG, "alarm %02d:%02d ringing", st.alarm_hour, st.alarm_minute);
    start_ringing();
}

// ---------------------------------------------------------------------------
// เมนูตั้งเวลา/วันที่/ปลุก
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
    } else if (item == MENU_DATE) {
        set_field(0, t.tm_mday, 1, 31);
        set_field(1, t.tm_mon + 1, 1, 12);             // tm_mon นับ 0–11
        set_field(2, t.tm_year + 1900 - 2000, 24, 99); // แสดงปี 2 หลัก (2024–2099)
        s_field_count = 3;
    } else {
        settings_t st;
        settings_get(&st);
        set_field(0, st.alarm_hour, 0, 23);
        set_field(1, st.alarm_minute, 0, 59);
        s_field_count = 2;
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
    if (timekeeping_is_valid()) {
        set_state(MODES[s_mode_index]);     // กลับไปโหมดที่อยู่ก่อนเข้าเมนู
    } else {
        set_state(UI_STATE_WAIT_TIME);
    }
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

static void save_time_or_date(void)
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
}

static void save_alarm(void)
{
    settings_t st;
    settings_get(&st);
    st.alarm_hour = s_fields[0].value;
    st.alarm_minute = s_fields[1].value;
    st.alarm_enabled = true;                            // ตั้งเวลาปลุกแล้ว = เปิดปลุกให้เลย
    esp_err_t err = settings_set(&st);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving alarm failed: %s", esp_err_to_name(err));
    }
}

static void save_edit(void)
{
    if (s_edit_item == MENU_ALARM) {
        save_alarm();
    } else {
        save_time_or_date();
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

// ปุ่มในโหมดหลักทั้ง 4 (DESIGN.md 6.1–6.2)
static void mode_handle_input(input_event_t ev)
{
    if (ev == INPUT_EVENT_PREV) {
        switch_mode(-1);
    } else if (ev == INPUT_EVENT_NEXT) {
        switch_mode(+1);
    } else if (ev == INPUT_EVENT_HOLD_3S) {
        open_menu();
    } else if (ev == INPUT_EVENT_ENTER) {
        if (s_state == UI_STATE_MODE_CLOCK) {
            set_state(UI_STATE_SHOW_NET_STATUS);
        } else if (s_state == UI_STATE_MODE_STOPWATCH) {
            stopwatch_enter();
        } else if (s_state == UI_STATE_MODE_ALARM) {
            alarm_toggle();
        }
        // MODE_DATE: ยังว่าง (DESIGN.md 10)
    }
    // PREV_REPEAT / NEXT_REPEAT: ไม่ใช้ในโหมดหลัก
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
    case UI_STATE_MODE_DATE:
    case UI_STATE_MODE_STOPWATCH:
    case UI_STATE_MODE_ALARM:
        mode_handle_input(ev);
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

    case UI_STATE_ALARM_RINGING:
        stop_ringing();                                 // แตะ/กดอะไรก็ได้ = หยุดปลุก
        break;

    default:
        break;                                          // PROVISIONING: ไม่รับปุ่ม
    }
}

// การเปลี่ยน state ที่ไม่ได้มาจากปุ่ม แต่มาจากเวลาหรือสถานะของโมดูลอื่น
static void update_state(void)
{
    uint32_t now = now_ms();

    check_alarm();

    switch (s_state) {
    case UI_STATE_BOOT:
        s_mode_index = 0;
        set_state(timekeeping_is_valid() ? UI_STATE_MODE_CLOCK : UI_STATE_WAIT_TIME);
        break;

    case UI_STATE_WAIT_TIME:
        if (timekeeping_is_valid()) {
            s_mode_index = 0;
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

    case UI_STATE_ALARM_RINGING:
        if (now - s_state_since_ms >= UI_ALARM_RING_MS) {
            ESP_LOGI(TAG, "alarm stopped after 60 s");
            stop_ringing();
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