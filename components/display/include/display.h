#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DISPLAY_WIDTH 32
#define DISPLAY_HEIGHT 8

/**
 * Off-screen framebuffer for the 32x8 panel. One bit per pixel, column-major:
 * cols[x] bit y (0 = top row, per docs/DESIGN.md section 5.1 layout).
 * Caller owns the storage; display_flush() only reads it.
 */
typedef struct {
    uint8_t cols[DISPLAY_WIDTH];
} display_framebuffer_t;

/**
 * Brings up the SPI bus and the MAX7219 chain (4x 8x8 modules).
 * Must be called once before any other display_* function.
 */
esp_err_t display_init(void);

/**
 * Sets global brightness. Clamp to 0-8 per docs/DESIGN.md section 2
 * (current draw through the VIN diode is the limiting factor).
 */
esp_err_t display_set_brightness(uint8_t level);

/** Clears a framebuffer to all-off. Hardware is untouched until display_flush(). */
void display_clear(display_framebuffer_t *fb);

/**
 * Draws a single pixel at (x, y) into the framebuffer. No-op if out of
 * [0, DISPLAY_WIDTH) x [0, DISPLAY_HEIGHT) bounds.
 */
void display_draw_pixel(display_framebuffer_t *fb, int x, int y, bool on);

/**
 * Draws one glyph at (x, y), where (x, y) is the glyph's top-left corner.
 * y may be negative or run past DISPLAY_HEIGHT - rows outside the visible
 * area must be clipped, not wrapped, so the roll-up animation in
 * docs/DESIGN.md section 5.2 can push digits off the top and in from the
 * bottom. glyph is glyph_height rows of glyph_width bits each, MSB-first per
 * row, supplied by a font table defined elsewhere (not in this component).
 */
void display_draw_glyph(display_framebuffer_t *fb, int x, int y,
                         const uint8_t *glyph, uint8_t glyph_width,
                         uint8_t glyph_height);

/** Sends the full framebuffer to the physical MAX7219 chain in one shot. */
esp_err_t display_flush(const display_framebuffer_t *fb);
