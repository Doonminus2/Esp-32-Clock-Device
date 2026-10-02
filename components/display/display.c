#include "display.h"

#include <string.h>

#include "board.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "max7219.h"

static const char *TAG = "display";

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

#define DISPLAY_MODULES             4        // four 8x8 matrices = 32x8
#define DISPLAY_SPI_CLOCK_HZ        1000000  // 1 MHz: safe with 3.3V logic and jumper wires
#define DISPLAY_MAX_BRIGHTNESS      8        // DESIGN.md section 2: keep VIN current in check
#define DISPLAY_DEFAULT_BRIGHTNESS  2

// Panel orientation. Run the smoke test and flip these (0 <-> 1) until
// step 2 reads "1 2 3 4" left to right, upright.
#define DISPLAY_REVERSE_MODULES     0  // set 1 if you read "4 3 2 1"
#define DISPLAY_MIRROR_COLUMNS      0  // set 1 if every digit looks mirrored left-right
#define DISPLAY_FLIP_ROWS           0  // set 1 if everything is upside down

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static max7219_t s_dev;      // driver descriptor from esp-idf-lib/max7219
static bool s_ready = false; // true once display_init() succeeded

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------

esp_err_t display_init(void)
{
    // 1. Start the SPI hardware (VSPI) on our DIN/CLK pins.
    //    The MAX7219 never sends data back, so there is no MISO pin (-1).
    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_PIN_DISPLAY_DIN,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_PIN_DISPLAY_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0, // 0 = use the driver default
    };
    esp_err_t err = spi_bus_initialize(BOARD_DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Tell the max7219 library about our chain of 4 chips and the CS pin.
    //    We do our own mirroring (see the orientation flags above), so the
    //    library's .mirrored stays false.
    s_dev = (max7219_t){
        .cascade_size = DISPLAY_MODULES,
        .digits = 0, // 0 = all digits (4 chips x 8 rows)
        .mirrored = false,
    };
    err = max7219_init_desc(&s_dev, BOARD_DISPLAY_SPI_HOST, DISPLAY_SPI_CLOCK_HZ,
                            BOARD_PIN_DISPLAY_CS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "max7219_init_desc failed: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Wake the chips up, clear them (library sets brightness 0 here).
    err = max7219_init(&s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "max7219_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;

    // 4. Start at a low, safe brightness.
    err = display_set_brightness(DISPLAY_DEFAULT_BRIGHTNESS);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "MAX7219 x%d on VSPI (DIN=%d CLK=%d CS=%d) at %lu Hz, brightness %d",
             DISPLAY_MODULES, BOARD_PIN_DISPLAY_DIN, BOARD_PIN_DISPLAY_CLK,
             BOARD_PIN_DISPLAY_CS, (unsigned long)DISPLAY_SPI_CLOCK_HZ,
             DISPLAY_DEFAULT_BRIGHTNESS);
    ESP_LOGI(TAG, "MAX7219 is write-only: check the LEDs to confirm the panel works");
    return ESP_OK;
}

esp_err_t display_set_brightness(uint8_t level)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE; // display_init() has not run yet
    }
    if (level > DISPLAY_MAX_BRIGHTNESS) {
        level = DISPLAY_MAX_BRIGHTNESS;
    }
    return max7219_set_brightness(&s_dev, level);
}

esp_err_t display_flush(const display_framebuffer_t *fb)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    // Our framebuffer stores one byte per COLUMN (cols[x], bit y = row y).
    // The MAX7219 on an FC-16 module wants one byte per ROW of each 8x8 module.
    // So for every module and every row we collect 8 column bits into a byte.
    for (int module = 0; module < DISPLAY_MODULES; module++) {
        for (int row = 0; row < 8; row++) {
            uint8_t bits = 0;
            for (int c = 0; c < 8; c++) {
                int x = module * 8 + c;
                if (fb->cols[x] & (1u << row)) {
                    // Default: leftmost column -> highest bit (0x80).
                    bits |= DISPLAY_MIRROR_COLUMNS ? (uint8_t)(1u << c)
                                                   : (uint8_t)(0x80u >> c);
                }
            }

            int chip = DISPLAY_REVERSE_MODULES ? (DISPLAY_MODULES - 1 - module) : module;
            int reg = DISPLAY_FLIP_ROWS ? (7 - row) : row;

            // The library numbers "digits" across the whole chain: chip * 8 + register.
            esp_err_t err = max7219_set_digit(&s_dev, (uint8_t)(chip * 8 + reg), bits);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "max7219_set_digit failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Drawing into the framebuffer (pure memory, no hardware)
// ---------------------------------------------------------------------------

void display_clear(display_framebuffer_t *fb)
{
    memset(fb->cols, 0, sizeof(fb->cols));
}

void display_draw_pixel(display_framebuffer_t *fb, int x, int y, bool on)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return; // outside the panel: clip
    }
    if (on) {
        fb->cols[x] |= (uint8_t)(1u << y);    // set bit y
    } else {
        fb->cols[x] &= (uint8_t)~(1u << y);   // clear bit y
    }
}

void display_draw_glyph(display_framebuffer_t *fb, int x, int y,
                        const uint8_t *glyph, uint8_t glyph_width,
                        uint8_t glyph_height)
{
    for (int row = 0; row < glyph_height; row++) {
        for (int col = 0; col < glyph_width; col++) {
            // bit (glyph_width - 1) is the leftmost pixel of the row
            bool on = glyph[row] & (1u << (glyph_width - 1 - col));
            if (on) {
                display_draw_pixel(fb, x + col, y + row, true); // clips for us
            }
        }
    }
}