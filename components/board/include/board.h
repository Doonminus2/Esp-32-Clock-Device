#pragma once

// TODO(me): define pin macros for the wiring in docs/DESIGN.md section 2.
// This header must stay hardware-only: pin numbers and simple derived
// constants, nothing else. No other component may define a GPIO number
// itself - they all pull from here.
//
// MAX7219 display chain (VSPI):
//   - BOARD_PIN_DISPLAY_DIN  -> GPIO23
//   - BOARD_PIN_DISPLAY_CLK  -> GPIO18
//   - BOARD_PIN_DISPLAY_CS   -> GPIO5
//
// DS1302 RTC:
//   - BOARD_PIN_RTC_CLK -> GPIO14
//   - BOARD_PIN_RTC_DAT -> GPIO27
//   - BOARD_PIN_RTC_RST -> GPIO26
//
// Push button (Enter / menu hold):
//   - BOARD_PIN_BUTTON_ENTER -> GPIO25
//   - smoke test TODO: find out if a press reads HIGH or LOW (docs/DESIGN.md
//     section 2, "ปุ่มกด: ยังไม่รู้") before wiring iot_button's active level.
//
// Touch buttons (TTP223, active HIGH out of the box):
//   - BOARD_PIN_TOUCH_PREV -> GPIO32 (T1, <)
//   - BOARD_PIN_TOUCH_NEXT -> GPIO33 (T2, >)
//
// Buzzer (LEDC PWM):
//   - BOARD_PIN_BUZZER -> GPIO19
//
// Reserved / do not use:
//   - GPIO6-11 (flash), GPIO12 (strapping)
// Spare pins available if something needs one later:
//   - GPIO4, GPIO13, GPIO16, GPIO17


#pragma once // include guard กันไม่ให่ header file ถูก include ซ้ำ

#include "driver/gpio.h"        // gpio_num_t, GPIO_NUM_xx
#include "driver/spi_common.h"  // spi_host_device_t, SPI3_HOST

// ---------- MAX7219 LED matrix (SPI) ----------
#define BOARD_DISPLAY_SPI_HOST   SPI3_HOST     // VSPI ตัว controllerที่ใช้กับ MAX7219 ให้รู้ว่ากำลังส่งงาน martix ตัวไหน
#define BOARD_PIN_DISPLAY_DIN    GPIO_NUM_23
#define BOARD_PIN_DISPLAY_CLK    GPIO_NUM_18
#define BOARD_PIN_DISPLAY_CS     GPIO_NUM_5

// ---------- DS1302 RTC ----------
#define BOARD_PIN_RTC_CLK        GPIO_NUM_14
#define BOARD_PIN_RTC_DAT        GPIO_NUM_27
#define BOARD_PIN_RTC_RST        GPIO_NUM_26

// ---------- Inputs ----------
#define BOARD_PIN_BUTTON_ENTER   GPIO_NUM_25
#define BOARD_PIN_TOUCH_PREV     GPIO_NUM_32   // T1
#define BOARD_PIN_TOUCH_NEXT     GPIO_NUM_33   // T2

// ---------- Buzzer ----------
#define BOARD_PIN_BUZZER         GPIO_NUM_19
