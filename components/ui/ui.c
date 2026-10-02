#include "ui.h"

#include <stdbool.h>
#include <time.h>

#include "display.h"
#include "display_font.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timekeeping.h"

static const char *TAG = "ui";

#define UI_TICK_MS      50     // the UI loop runs every 50 ms (20 times per second)
#define UI_BLINK_MS     500    // "--:--" blinks on/off every 500 ms
#define UI_TASK_STACK   4096   // bytes of stack for the ui task
#define UI_TASK_PRIO    5      // FreeRTOS priority (higher = more important)

static ui_state_t s_state = UI_STATE_BOOT;
static display_framebuffer_t s_fb;   // what we draw into before sending to the panel

// ---------------------------------------------------------------------------
// Drawing helpers (layout ก, docs/DESIGN.md section 5.1)
// ---------------------------------------------------------------------------

static void draw_big(int x, int digit)
{
    display_draw_glyph(&s_fb, x, 0, font_big_digits[digit], FONT_BIG_W, FONT_BIG_H);
}

static void draw_small(int x, int digit)
{
    // y = 2 lines the 5-row digits up with the bottom of the 7-row digits
    display_draw_glyph(&s_fb, x, 2, font_small_digits[digit], FONT_SMALL_W, FONT_SMALL_H);
}

static void draw_colons(void)
{
    display_draw_pixel(&s_fb, 10, 2, true);   // big colon between HH and MM
    display_draw_pixel(&s_fb, 10, 4, true);
    display_draw_pixel(&s_fb, 22, 3, true);   // small colon before seconds
    display_draw_pixel(&s_fb, 22, 5, true);
}

// HH:MM in big digits + SS in small digits
static void draw_clock(const struct tm *t)
{
    display_clear(&s_fb);
    draw_big(0,  t->tm_hour / 10);
    draw_big(5,  t->tm_hour % 10);
    draw_big(12, t->tm_min / 10);
    draw_big(17, t->tm_min % 10);
    draw_small(24, t->tm_sec / 10);
    draw_small(28, t->tm_sec % 10);
    draw_colons();
}

// "--:--" shown while we have no valid time yet
static void draw_no_time(bool visible)
{
    display_clear(&s_fb);
    if (!visible) {
        return; // blank half of the blink
    }
    const int dash_x[] = {0, 5, 12, 17};
    for (int i = 0; i < 4; i++) {
        for (int dx = 0; dx < FONT_BIG_W; dx++) {
            display_draw_pixel(&s_fb, dash_x[i] + dx, 3, true); // a dash on row 3
        }
    }
    display_draw_pixel(&s_fb, 10, 2, true);
    display_draw_pixel(&s_fb, 10, 4, true);
}

// ---------------------------------------------------------------------------
// The UI task
// ---------------------------------------------------------------------------

static void ui_task(void *arg)
{
    int last_sec = -1;     // last second we drew, so we only redraw on change
    int last_blink = -1;   // last blink phase we drew

    while (1) {
        if (timekeeping_is_valid()) {
            s_state = UI_STATE_MODE_CLOCK;

            struct tm now;
            timekeeping_now(&now);
            if (now.tm_sec != last_sec) {          // a new second: redraw
                last_sec = now.tm_sec;
                draw_clock(&now);
                display_flush(&s_fb);
            }
        } else {
            s_state = UI_STATE_WAIT_TIME;

            // xTaskGetTickCount() = time since boot in ticks; divide into 500 ms slots
            int blink = (xTaskGetTickCount() / pdMS_TO_TICKS(UI_BLINK_MS)) % 2;
            if (blink != last_blink) {
                last_blink = blink;
                draw_no_time(blink == 0);
                display_flush(&s_fb);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));    // sleep, let other tasks run
    }
}

esp_err_t ui_init(void)
{
    // Start ui_task running in the background. It never returns.
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