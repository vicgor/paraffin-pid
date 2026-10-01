#pragma once
// Абстракция дисплея. UI рисует кадр целиком через Adafruit_GFX в абсолютных координатах:
//  - OLED: в собственный буфер драйвера, затем display();
//  - TFT : по горизонтальным полосам в небольшой RAM-буфер и DMA/SPI-выгрузка —
//          без мерцания при любом размере экрана и без полного кадрового буфера.
#include <Adafruit_GFX.h>

#include <functional>

#include "display_config.h"

// Палитра RGB565; на монохромных дисплеях всё, кроме BG, превращается в «белый».
namespace pal {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t FG = 0xFFFF;
constexpr uint16_t DIM = 0x8410;
constexpr uint16_t ACCENT = 0x07FF;  // циан
constexpr uint16_t OK = 0x07E0;
constexpr uint16_t WARN = 0xFD20;  // оранжевый
constexpr uint16_t ERR = 0xF800;
constexpr uint16_t SEL = 0x2945;   // фон выделенной строки меню
constexpr uint16_t GRID = 0x2104;
}  // namespace pal

class Display {
public:
    using RenderFn = std::function<void(Adafruit_GFX&)>;
    virtual ~Display() = default;
    virtual bool begin() = 0;
    virtual void render(const RenderFn& fn) = 0;
    virtual bool isColor() const = 0;
    int16_t width() const { return DISP_W; }
    int16_t height() const { return DISP_H; }
    // Цвет с учётом монохромности
    uint16_t c(uint16_t color) const { return isColor() ? color : (color == pal::BG ? 0 : 1); }
};

Display* createDisplay();  // фабрика по флагам сборки (nullptr при DISPLAY_NONE)
