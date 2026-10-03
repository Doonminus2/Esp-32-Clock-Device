#pragma once

#include <stdint.h>

// Digit fonts for the clock layout (docs/DESIGN.md section 5.1).
// One byte per row, top row first. Bit (width - 1) is the leftmost pixel,
// matching display_draw_glyph().


// config marcos for the fonts
#define FONT_BIG_W   4   // HH:MM digits
#define FONT_BIG_H   7
#define FONT_SMALL_W 3   // seconds digits
#define FONT_SMALL_H 5

extern const uint8_t font_big_digits[10][FONT_BIG_H];
extern const uint8_t font_small_digits[10][FONT_SMALL_H];

// 3x5 glyph for one character: '0'-'9', 'A'-'Z', '/', '-'.
// Returns NULL for anything else (e.g. a space): draw nothing, just advance.
const uint8_t *font_small_glyph(char c);