/*
 * buzzer.c — passive buzzer บน GPIO19 ขับด้วย LEDC (PWM)
 *
 * passive buzzer ไม่มีวงจรสร้างเสียงในตัว เราต้องส่งคลื่นสี่เหลี่ยมความถี่เสียง
 * (เช่น 2700 Hz) ให้มันเอง → ใช้ LEDC ซึ่งเป็นฮาร์ดแวร์สร้าง PWM ของ ESP32
 *   duty = 50%  → มีเสียง
 *   duty = 0    → เงียบ
 *
 * โครงสร้าง: มี task "buzzer" ตัวเดียวเป็นเจ้าของ LEDC
 * ฟังก์ชัน buzzer_play()/buzzer_stop() แค่ "ฝากคำสั่ง" ลง queue แล้วกลับทันที
 * ui จึงไม่ต้องหยุดรอเสียง beep จบ (ไม่บล็อกการวาดจอ)
 */
#include "buzzer.h"

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "buzzer";

// ---------------------------------------------------------------------------
// ค่าตั้ง
// ---------------------------------------------------------------------------

#define BUZZER_FREQ_HZ      2700              // ความถี่เสียง ลองเปลี่ยน 2000–4000 ดูว่าเสียงไหนดังสุด
#define BUZZER_RESOLUTION   LEDC_TIMER_10_BIT // duty มีค่าได้ 0–1023
#define BUZZER_DUTY_ON      512               // 512/1024 = 50% = ดังที่สุดสำหรับ passive buzzer
#define BUZZER_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER        LEDC_TIMER_0
#define BUZZER_CHANNEL      LEDC_CHANNEL_0

#define BUZZER_TASK_STACK   2048
#define BUZZER_TASK_PRIO    4
#define BUZZER_QUEUE_LEN    4

#define BUZZER_CMD_STOP     (-1)  // คำสั่งพิเศษในคิว: หยุดเสียง

// ---------------------------------------------------------------------------
// รูปแบบเสียง (DESIGN.md 6.3)
// แต่ละขั้น = ดัง on_ms แล้วเงียบ off_ms
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} beep_step_t;

typedef struct {
    const beep_step_t *steps; // ชี้ไปที่ array ของขั้น
    uint8_t count;            // จำนวนขั้น
    bool repeat;              // true = เล่นวนจนกว่าจะสั่งหยุด
} beep_pattern_t;

static const beep_step_t STEPS_MENU_ENTER[] = { {120, 0} };
static const beep_step_t STEPS_WIFI_ENTER[] = { {100, 100}, {100, 0} };
static const beep_step_t STEPS_SAVE[]       = { {40, 0} };
static const beep_step_t STEPS_ALARM[]      = { {100, 100}, {100, 600} };

// [ชื่อ enum] = ... เรียกว่า designated initializer: ช่องที่ตรงกับ enum นั้นๆ
static const beep_pattern_t PATTERNS[] = {
    [BUZZER_PATTERN_MENU_ENTER] = { STEPS_MENU_ENTER, 1, false },
    [BUZZER_PATTERN_WIFI_ENTER] = { STEPS_WIFI_ENTER, 2, false },
    [BUZZER_PATTERN_SAVE]       = { STEPS_SAVE,       1, false },
    [BUZZER_PATTERN_ALARM]      = { STEPS_ALARM,      2, true  },
};
#define PATTERN_COUNT (sizeof(PATTERNS) / sizeof(PATTERNS[0]))

static QueueHandle_t s_queue = NULL;

// ---------------------------------------------------------------------------
// LEDC
// ---------------------------------------------------------------------------

static void tone(bool on)
{
    ledc_set_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL, on ? BUZZER_DUTY_ON : 0);
    ledc_update_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL); // set_duty แค่เตรียมค่า ต้อง update ถึงจะมีผล
}

// ---------------------------------------------------------------------------
// task ที่เล่นเสียง
// ---------------------------------------------------------------------------

static void buzzer_task(void *arg)
{
    const beep_pattern_t *pat = NULL; // รูปแบบที่กำลังเล่น (NULL = เงียบ)
    int step = 0;                     // ขั้นที่เท่าไหร่ในรูปแบบ
    bool sounding = false;            // ตอนนี้อยู่ช่วง "ดัง" หรือ "เงียบ"

    while (1) {
        // รอนานเท่าไหร่: ถ้าไม่มีอะไรเล่นอยู่ รอคำสั่งไปเรื่อยๆ (portMAX_DELAY)
        // ถ้ากำลังเล่น รอแค่ความยาวของช่วงปัจจุบัน
        TickType_t wait = portMAX_DELAY;
        if (pat != NULL) {
            uint16_t ms = sounding ? pat->steps[step].on_ms : pat->steps[step].off_ms;
            wait = pdMS_TO_TICKS(ms);
        }

        int cmd;
        if (xQueueReceive(s_queue, &cmd, wait) == pdTRUE) {
            // ได้คำสั่งใหม่ → ยกเลิกของเก่าทันที
            if (cmd == BUZZER_CMD_STOP) {
                tone(false);
                pat = NULL;
            } else {
                pat = &PATTERNS[cmd];
                step = 0;
                sounding = true;
                tone(true);
            }
            continue;
        }

        // หมดเวลาของช่วงปัจจุบัน (ไม่มีคำสั่งใหม่เข้ามา)
        if (sounding) {
            tone(false);          // ช่วงดังจบ → ไปช่วงเงียบของขั้นเดิม
            sounding = false;
            continue;
        }

        step++;                   // ช่วงเงียบจบ → ไปขั้นถัดไป
        if (step >= pat->count) {
            if (!pat->repeat) {
                pat = NULL;       // เล่นครบแล้ว
                continue;
            }
            step = 0;             // ALARM: วนกลับไปขั้นแรก
        }
        sounding = true;
        tone(true);
    }
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

esp_err_t buzzer_init(void)
{
    // 1. timer: กำหนดความถี่ของคลื่น
    ledc_timer_config_t timer = {
        .speed_mode = BUZZER_SPEED_MODE,
        .duty_resolution = BUZZER_RESOLUTION,
        .timer_num = BUZZER_TIMER,
        .freq_hz = BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc_timer_config failed");

    // 2. channel: ผูก timer เข้ากับขา GPIO ของ buzzer เริ่มที่ duty 0 (เงียบ)
    ledc_channel_config_t channel = {
        .gpio_num = BOARD_PIN_BUZZER,
        .speed_mode = BUZZER_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "ledc_channel_config failed");

    // 3. คิวคำสั่ง + task
    s_queue = xQueueCreate(BUZZER_QUEUE_LEN, sizeof(int));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(buzzer_task, "buzzer", BUZZER_TASK_STACK, NULL,
                    BUZZER_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ready on GPIO%d, %d Hz", BOARD_PIN_BUZZER, BUZZER_FREQ_HZ);
    return ESP_OK;
}

esp_err_t buzzer_play(buzzer_pattern_t pattern)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;  // buzzer_init() ไม่ผ่าน
    }
    if ((unsigned)pattern >= PATTERN_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    int cmd = pattern;
    // timeout 0 = ถ้าคิวเต็มก็ไม่รอ (เสียงหายไปหนึ่งครั้งดีกว่าทำให้ ui ค้าง)
    return xQueueSend(s_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t buzzer_stop(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int cmd = BUZZER_CMD_STOP;
    return xQueueSend(s_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}