#pragma once
// ESP32-C6-DevKitC-1. Избегаем strapping-пинов (4, 5, 8, 9, 15), USB (12, 13), UART0 (16, 17).
// GPIO0/1 свободны, если на плате нет кварца 32 кГц (на DevKitC-1 нет).
#define APP_BOARD_NAME "ESP32-C6 DevKitC-1"

#define PIN_ONEWIRE   3
#define PIN_SSR       2
#define PIN_BUZZER    11

#define PIN_ENC_A     18
#define PIN_ENC_B     19
#define PIN_ENC_BTN   20

#define PIN_I2C_SDA   6
#define PIN_I2C_SCL   7

#define PIN_TFT_SCK   21
#define PIN_TFT_MOSI  22
#define PIN_TFT_CS    23
#define PIN_TFT_DC    0
#define PIN_TFT_RST   1
#define PIN_TFT_BL    10
