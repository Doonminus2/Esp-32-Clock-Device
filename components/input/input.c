/*
 * input.c — ทัช T1/T2 (TTP223) + ปุ่มกด Enter ผ่าน espressif/button
 *
 * espressif/button อ่านขาทุก 5 ms ด้วย esp_timer ทำ debounce ให้ แล้วเรียก
 * callback ของเราตามเหตุการณ์ (กดลง, คลิก, กดค้าง ...)
 * callback ของเราแค่แปลงเป็น input_event_t แล้วโยนลง queue ให้ ui มาหยิบไป
 * input ไม่เรียก ui หรือ buzzer เอง (กฎการพึ่งพา DESIGN.md ข้อ 4)
 */
#include "input.h"

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "sdkconfig.h"

static const char *TAG = "input";

// ---------------------------------------------------------------------------
// ค่าตั้ง
// ---------------------------------------------------------------------------

#define INPUT_TOUCH_ACTIVE_LEVEL    1    // TTP223 ค่าโรงงาน: แตะ = HIGH
#define INPUT_BUTTON_ACTIVE_LEVEL   1    // ⚠ เดาไว้ก่อน: กด = LOW  (ดู log ตอนบูตแล้วแก้)

#define INPUT_QUEUE_LEN             16
#define INPUT_TOUCH_REPEAT_START_MS 500  // แตะค้างนานเท่านี้ถึงเริ่มวิ่งเร็ว
#define INPUT_TOUCH_REPEAT_MS       150  // ตอนวิ่งเร็ว ส่ง event ทุกๆ กี่ ms
#define INPUT_ENTER_HOLD_MS         3000 // ปุ่มกดค้าง 3 วิ = เข้าเมนู
#define INPUT_BOOT_HOLD_MS          500  // ตอนบูตต้องกดค้างต่อเนื่องเท่านี้ ถึงนับว่า "ค้างตอนเสียบไฟ"
#define INPUT_BOOT_SAMPLE_MS        10

// LONG_PRESS_HOLD ของ library ยิงทุก CONFIG_BUTTON_LONG_PRESS_HOLD_SERIAL_TIME_MS (20 ms)
// เร็วเกินไป → ส่งต่อแค่ 1 ครั้งในทุก N ครั้ง
#define INPUT_REPEAT_EVERY \
    (INPUT_TOUCH_REPEAT_MS / CONFIG_BUTTON_LONG_PRESS_HOLD_SERIAL_TIME_MS)

static QueueHandle_t s_queue = NULL;

// true ระหว่างที่ปุ่มที่กดค้างตอนบูตยังไม่ถูกปล่อย
// กันไม่ให้การกดครั้งนั้นกลายเป็น ENTER หรือ HOLD_3S ซ้ำอีก
// volatile: ตัวแปรนี้ถูกเขียนจาก callback ที่รันอยู่คนละ task
static volatile bool s_ignore_enter = false;

// ชื่อ event ไว้พิมพ์ log (ลำดับต้องตรงกับ enum ใน input.h)
static const char *const EVENT_NAMES[] = {
    "PREV", "NEXT", "PREV_REPEAT", "NEXT_REPEAT", "ENTER", "HOLD_3S", "BOOT_HOLD",
};

// ---------------------------------------------------------------------------
// ส่ง event เข้า queue
// ---------------------------------------------------------------------------

static void send_event(input_event_t ev)
{
    // repeat ยิงถี่ ไม่ต้อง log ทุกครั้ง
    if (ev != INPUT_EVENT_PREV_REPEAT && ev != INPUT_EVENT_NEXT_REPEAT) {
        ESP_LOGI(TAG, "event %s", EVENT_NAMES[ev]);
    }
    // timeout 0: ถ้าคิวเต็ม (ui ค้าง) ทิ้ง event ไป ห้ามรอใน callback
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped %s", EVENT_NAMES[ev]);
    }
}

// ---------------------------------------------------------------------------
// callback ของ espressif/button
// รูปแบบ: void cb(void *button_handle, void *usr_data)
// usr_data = ค่าที่เราส่งตอน register (ใช้ฝากว่าจะส่ง event อะไร)
// ---------------------------------------------------------------------------

// แตะทัช: ส่ง event ทันทีที่นิ้วแตะ (ไม่ต้องรอปล่อย ตอบสนองไว)
static void on_touch_down(void *btn, void *usr_data)
{
    send_event((input_event_t)(intptr_t)usr_data);
}

// แตะทัชค้าง: ส่ง REPEAT ทุก INPUT_TOUCH_REPEAT_MS
static void on_touch_hold(void *btn, void *usr_data)
{
    if (iot_button_get_long_press_hold_cnt(btn) % INPUT_REPEAT_EVERY == 0) {
        send_event((input_event_t)(intptr_t)usr_data);
    }
}

// ปุ่มกด: กดแล้วปล่อย (ไม่ถึง 3 วิ)
static void on_enter_click(void *btn, void *usr_data)
{
    if (!s_ignore_enter) {
        send_event(INPUT_EVENT_ENTER);
    }
}

// ปุ่มกด: ครบ 3 วิขณะยังกดอยู่ → ยิงทันที ui จะได้ beep บอกว่าปล่อยได้แล้ว
static void on_enter_hold(void *btn, void *usr_data)
{
    if (!s_ignore_enter) {
        send_event(INPUT_EVENT_HOLD_3S);
    }
}

// ปุ่มกด: จบการกดหนึ่งรอบ (ปล่อยปุ่มแล้ว)
static void on_enter_end(void *btn, void *usr_data)
{
    s_ignore_enter = false;
}

// ---------------------------------------------------------------------------
// ตัวช่วย
// ---------------------------------------------------------------------------

static esp_err_t create_button(gpio_num_t pin, uint8_t active_level,
                               uint16_t long_press_ms, button_handle_t *out)
{
    const button_config_t cfg = {
        .long_press_time = long_press_ms,
        .short_press_time = 0,          // 0 = ใช้ค่า default ของ library (180 ms)
    };
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = pin,
        .active_level = active_level,
        .enable_power_save = false,
        .disable_pull = false,          // ให้ library เปิด pull-up/pull-down ภายในตาม active_level
    };
    return iot_button_new_gpio_device(&cfg, &gpio_cfg, out);
}

// อ่านขาปุ่มกดตรงๆ ตอนบูต (ก่อน espressif/button เริ่มทำงาน)
// คืน true ถ้าปุ่มถูกกดค้างต่อเนื่อง INPUT_BOOT_HOLD_MS
static bool check_boot_hold(void)
{
    const gpio_num_t pin = BOARD_PIN_BUTTON_ENTER;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = INPUT_BUTTON_ACTIVE_LEVEL == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = INPUT_BUTTON_ACTIVE_LEVEL == 1 ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(INPUT_BOOT_SAMPLE_MS)); // รอให้แรงดันนิ่ง

    // smoke test: บรรทัดนี้บอกว่าตอนไม่กด ขาอ่านได้เท่าไหร่
    ESP_LOGI(TAG, "push button GPIO%d reads %d now (active level set to %d)",
             pin, gpio_get_level(pin), INPUT_BUTTON_ACTIVE_LEVEL);

    for (int ms = 0; ms < INPUT_BOOT_HOLD_MS; ms += INPUT_BOOT_SAMPLE_MS) {
        if (gpio_get_level(pin) != INPUT_BUTTON_ACTIVE_LEVEL) {
            return false;  // ปล่อยระหว่างทาง (หรือไม่ได้กดเลย)
        }
        vTaskDelay(pdMS_TO_TICKS(INPUT_BOOT_SAMPLE_MS));
    }
    ESP_LOGW(TAG, "button held at power-on -> WiFi setup. If you were NOT "
                  "holding it, change INPUT_BUTTON_ACTIVE_LEVEL to %d",
             !INPUT_BUTTON_ACTIVE_LEVEL);
    return true;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

esp_err_t input_init(void)
{
    s_queue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(input_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // ต้องเช็กก่อนสร้างปุ่ม ไม่งั้น library จะเห็นการกดค้างนี้เป็นการกดปกติ
    bool boot_hold = check_boot_hold();
    s_ignore_enter = boot_hold;

    button_handle_t prev, next, enter;
    ESP_RETURN_ON_ERROR(create_button(BOARD_PIN_TOUCH_PREV, INPUT_TOUCH_ACTIVE_LEVEL,
                                      INPUT_TOUCH_REPEAT_START_MS, &prev),
                        TAG, "T1 (prev) failed");
    ESP_RETURN_ON_ERROR(create_button(BOARD_PIN_TOUCH_NEXT, INPUT_TOUCH_ACTIVE_LEVEL,
                                      INPUT_TOUCH_REPEAT_START_MS, &next),
                        TAG, "T2 (next) failed");
    ESP_RETURN_ON_ERROR(create_button(BOARD_PIN_BUTTON_ENTER, INPUT_BUTTON_ACTIVE_LEVEL,
                                      INPUT_ENTER_HOLD_MS, &enter),
                        TAG, "push button failed");

    // (void *)(intptr_t)X = ฝากเลข enum ไว้ในช่อง pointer แล้วแปลงกลับใน callback
    ESP_RETURN_ON_ERROR(iot_button_register_cb(prev, BUTTON_PRESS_DOWN, NULL, on_touch_down,
                                               (void *)(intptr_t)INPUT_EVENT_PREV), TAG, "cb");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(prev, BUTTON_LONG_PRESS_HOLD, NULL, on_touch_hold,
                                               (void *)(intptr_t)INPUT_EVENT_PREV_REPEAT), TAG, "cb");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(next, BUTTON_PRESS_DOWN, NULL, on_touch_down,
                                               (void *)(intptr_t)INPUT_EVENT_NEXT), TAG, "cb");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(next, BUTTON_LONG_PRESS_HOLD, NULL, on_touch_hold,
                                               (void *)(intptr_t)INPUT_EVENT_NEXT_REPEAT), TAG, "cb");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(enter, BUTTON_SINGLE_CLICK, NULL,
                                               on_enter_click, NULL), TAG, "cb");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(enter, BUTTON_LONG_PRESS_START, NULL,
                                               on_enter_hold, NULL), TAG, "cb");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(enter, BUTTON_PRESS_END, NULL,
                                               on_enter_end, NULL), TAG, "cb");

    if (boot_hold) {
        send_event(INPUT_EVENT_BOOT_HOLD); // อยู่ในคิวรอ ui เริ่มทำงาน
    }
    return ESP_OK;
}

QueueHandle_t input_get_event_queue(void)
{
    return s_queue;
}