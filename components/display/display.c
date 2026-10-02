#include "display.h"

esp_err_t display_init(void)
{
    // TODO(me):
    // 1. Pull pin numbers from board.h (DIN/CLK/CS).
    // 2. Configure the VSPI bus (spi_bus_config_t + spi_bus_initialize).
    // 3. Build a max7219_t descriptor and call max7219_init_desc() then
    //    max7219_init() for the 4-module chain (see esp-idf-lib max7219
    //    examples under managed_components/esp-idf-lib__max7219/examples).
    // 4. Set an initial safe brightness (<= 8/15 per docs/DESIGN.md section 2).
    return ESP_OK;
}

esp_err_t display_set_brightness(uint8_t level)
{
    // TODO(me):
    // 1. Clamp level to [0, 8].
    // 2. Call max7219_set_brightness() with the clamped value.
    return ESP_OK;
}

void display_clear(display_framebuffer_t *fb)
{
    // TODO(me): zero every column in fb->cols.
}

void display_draw_pixel(display_framebuffer_t *fb, int x, int y, bool on)
{
    // TODO(me):
    // 1. Bounds-check x against [0, DISPLAY_WIDTH) and y against
    //    [0, DISPLAY_HEIGHT); return early if out of range.
    // 2. Set or clear bit y of fb->cols[x] depending on `on`.
}

void display_draw_glyph(display_framebuffer_t *fb, int x, int y,
                         const uint8_t *glyph, uint8_t glyph_width,
                         uint8_t glyph_height)
{
    // TODO(me):
    // 1. Loop glyph rows 0..glyph_height-1 and columns 0..glyph_width-1.
    // 2. For each set bit in the glyph row, compute the destination
    //    (x + col, y + row) and call display_draw_pixel(), which already
    //    clips out-of-bounds coordinates.
}

esp_err_t display_flush(const display_framebuffer_t *fb)
{
    // TODO(me):
    // 1. Walk fb->cols and push each column to the right MAX7219 module
    //    and digit index (mapping depends on module chain order / mirroring
    //    - see docs/DESIGN.md section 10 open question on panel orientation).
    // 2. Use max7219_draw_image_8x8() per module, or set digits directly.
    return ESP_OK;
}
