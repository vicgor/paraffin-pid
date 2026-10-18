#pragma once
// Waveshare RP2350-LCD-0.96 (RP2350A, ST7735S 160x80 IPS на SPI1, Li-Po разъём).
// Пины дисплея разведены на плате: GP8..GP12 и GP25 заняты.
// RP2350 A2 (errata E9): для кнопок только INPUT_PULLUP.
#define APP_BOARD_NAME "Waveshare RP2350-LCD-0.96"

#define PIN_ONEWIRE   2
#define PIN_SSR       3
#define PIN_BUZZER    14

#define PIN_ENC_A     6
#define PIN_ENC_B     7
#define PIN_ENC_BTN   13

// I2C0 (Wire): GP4/GP5
#define PIN_I2C_SDA   4
#define PIN_I2C_SCL   5

// Встроенный LCD, SPI1: SCK GP10, TX GP11
#define PIN_TFT_SCK   10
#define PIN_TFT_MOSI  11
#define PIN_TFT_CS    9
#define PIN_TFT_DC    8
#define PIN_TFT_RST   12
#define PIN_TFT_BL    25
