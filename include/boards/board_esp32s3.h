#pragma once
// ESP32-S3-DevKitC-1 (N8R2/N16R8). Пины можно переопределить через build_flags (-DPIN_SSR=...)
#define APP_BOARD_NAME "ESP32-S3 DevKitC-1"

#define PIN_ONEWIRE   4   // DS18B20 (DQ), подтяжка 4.7 кОм к 3.3 В
#define PIN_SSR       5   // вход SSR (+), активный уровень HIGH
#define PIN_BUZZER    16  // активный зуммер (через транзистор), -1 — нет

#define PIN_ENC_A     6
#define PIN_ENC_B     7
#define PIN_ENC_BTN   15

// I2C OLED
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9

// SPI TFT
#define PIN_TFT_SCK   12
#define PIN_TFT_MOSI  11
#define PIN_TFT_CS    10
#define PIN_TFT_DC    13
#define PIN_TFT_RST   14
#define PIN_TFT_BL    21
