#include "display_test.h"

#include "display.h"
#include "display_font.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_test";

static display_framebuffer_t fb;

// Send the framebuffer to the panel, then wait.
static void show(uint32_t ms)
{
    ESP_ERROR_CHECK(display_flush(&fb));
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void draw_big(int x, int digit)
{
    display_draw_glyph(&fb, x, 0, font_big_digits[digit], FONT_BIG_W, FONT_BIG_H);
}

static void draw_small(int x, int digit)
{
    // y = 2 so the 5-row digits line up with the bottom of the 7-row digits
    display_draw_glyph(&fb, x, 2, font_small_digits[digit], FONT_SMALL_W, FONT_SMALL_H);
}

// Layout ก from docs/DESIGN.md section 5.1
static void draw_clock(int hh, int mm, int ss)
{
    display_clear(&fb);
    draw_big(0, hh / 10);
    draw_big(5, hh % 10);
    display_draw_pixel(&fb, 10, 2, true);   // colon
    display_draw_pixel(&fb, 10, 4, true);
    draw_big(12, mm / 10);
    draw_big(17, mm % 10);
    display_draw_pixel(&fb, 22, 3, true);   // small colon
    display_draw_pixel(&fb, 22, 5, true);
    draw_small(24, ss / 10);
    draw_small(28, ss % 10);
}

void display_smoke_test(void)
{
    // Step 1: every LED on. Checks power and that all 4 modules respond.
    ESP_LOGI(TAG, "step 1/4: ALL LEDs ON for 3 s - all 4 modules should be fully lit");
    display_clear(&fb);
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            display_draw_pixel(&fb, x, y, true);
        }
    }
    show(3000);

    // Step 2: orientation. One digit per module + a dot in the top-left corner.
    ESP_LOGI(TAG, "step 2/4: should read '1 2 3 4' left to right, upright,");
    ESP_LOGI(TAG, "          with one dot in the TOP-LEFT corner (5 s)");
    display_clear(&fb);
    for (int m = 0; m < 4; m++) {
        draw_big(m * 8 + 2, m + 1);
    }
    display_draw_pixel(&fb, 0, 0, true);
    show(5000);

    // Step 3: a dot walks along the top row, left to right.
    ESP_LOGI(TAG, "step 3/4: a dot walks along the TOP row, LEFT to RIGHT");
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        display_clear(&fb);
        display_draw_pixel(&fb, x, 0, true);
        show(80);
    }

    // Step 4: the real clock layout, counting up forever (no animation yet).
    ESP_LOGI(TAG, "step 4/4: clock layout counting from 12:34:50 (runs forever)");
    int hh = 12, mm = 34, ss = 50;
    while (1) {
        draw_clock(hh, mm, ss);
        show(1000);
        if (++ss == 60) { ss = 0; mm++; }
        if (mm == 60)   { mm = 0; hh++; }
        if (hh == 24)   { hh = 0; }
    }
}