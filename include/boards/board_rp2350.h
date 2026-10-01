#pragma once
// Raspberry Pi Pico 2 / Pico 2 W (RP2350). Wi-Fi — только на Pico 2 W.
#define APP_BOARD_NAME "Raspberry Pi Pico 2"

#define PIN_ONEWIRE   2
#define PIN_SSR       3
#define PIN_BUZZER    9

#define PIN_ENC_A     6
#define PIN_ENC_B     7
#define PIN_ENC_BTN   8

// I2C0 (Wire): GP4/GP5
#define PIN_I2C_SDA   4
#define PIN_I2C_SCL   5

// SPI0 (SPI): SCK GP18, TX GP19
#define PIN_TFT_SCK   18
#define PIN_TFT_MOSI  19
#define PIN_TFT_CS    17
#define PIN_TFT_DC    20
#define PIN_TFT_RST   21
#define PIN_TFT_BL    22
