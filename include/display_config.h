#pragma once
// Выбор дисплея — ровно один DISPLAY_* через build_flags:
//   -DDISPLAY_SSD1306              OLED 0.96" 128x64 I2C
//   -DDISPLAY_SH1106               OLED 1.3"  128x64 I2C
//   -DDISPLAY_ST7735 -DDISP_W=160 -DDISP_H=80    TFT 0.96" 80x160 (IPS)
//   -DDISPLAY_ST7735 -DDISP_W=160 -DDISP_H=128   TFT 1.8"  128x160
//   -DDISPLAY_ST7789 -DDISP_W=240 -DDISP_H=135   TFT 1.14" 135x240
//   -DDISPLAY_ST7789 -DDISP_W=240 -DDISP_H=240   TFT 1.3"/1.54" 240x240
//   -DDISPLAY_ST7789 -DDISP_W=320 -DDISP_H=240   TFT 2.0"/2.4" 240x320
//   -DDISPLAY_ILI9341                            TFT 2.4"/2.8" 240x320
//   -DDISPLAY_NONE                               без дисплея
// DISP_W/DISP_H — размер в ландшафтной ориентации (ширина ≥ высоты).

#if !defined(DISPLAY_SSD1306) && !defined(DISPLAY_SH1106) && !defined(DISPLAY_ST7735) && \
    !defined(DISPLAY_ST7789) && !defined(DISPLAY_ILI9341) && !defined(DISPLAY_NONE)
#define DISPLAY_SSD1306
#endif

#if defined(DISPLAY_SSD1306) || defined(DISPLAY_SH1106)
#ifndef DISP_W
#define DISP_W 128
#endif
#ifndef DISP_H
#define DISP_H 64
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif
#endif

#if defined(DISPLAY_ILI9341)
#define DISP_W 320
#define DISP_H 240
#endif

#if defined(DISPLAY_ST7735)
#ifndef DISP_W
#define DISP_W 160
#endif
#ifndef DISP_H
#define DISP_H 80
#endif
// Для китайских IPS 0.96" обычно нужен INITR_MINI160x80_PLUGIN (инверсия цветов).
// Если цвета «негативные» — замените на INITR_MINI160x80. Для 1.8" — INITR_BLACKTAB.
#ifndef ST7735_TAB
#if DISP_H == 80
#define ST7735_TAB INITR_MINI160x80_PLUGIN
#else
#define ST7735_TAB INITR_BLACKTAB
#endif
#endif
#endif

#if defined(DISPLAY_ST7789)
#ifndef DISP_W
#define DISP_W 320
#endif
#ifndef DISP_H
#define DISP_H 240
#endif
#endif

#ifndef DISP_ROTATION
#define DISP_ROTATION 1  // 1 или 3 — ландшафт; 3 — повернуть на 180°
#endif
#ifndef DISP_SPI_HZ
#define DISP_SPI_HZ 40000000
#endif
#ifndef DISP_BAND_H
#define DISP_BAND_H 40  // высота полосы буфера отрисовки (RAM = W*BAND_H*2 байт)
#endif
