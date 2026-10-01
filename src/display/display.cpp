#include "display.h"

#include <SPI.h>
#include <Wire.h>

#include "../hal.h"
#include "pins.h"

// ======================================================================= OLED
#if defined(DISPLAY_SSD1306) || defined(DISPLAY_SH1106)
#if defined(DISPLAY_SSD1306)
#include <Adafruit_SSD1306.h>
using OledDriver = Adafruit_SSD1306;
#else
#include <Adafruit_SH110X.h>
using OledDriver = Adafruit_SH1106G;
#endif

class OledDisplay : public Display {
public:
    OledDisplay() : oled_(DISP_W, DISP_H, &Wire, -1) {}
    bool begin() override {
#if PLATFORM_ESP32
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
#else
        Wire.setSDA(PIN_I2C_SDA);
        Wire.setSCL(PIN_I2C_SCL);
        Wire.begin();
#endif
        Wire.setClock(400000);
#if defined(DISPLAY_SSD1306)
        if (!oled_.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) return false;
#else
        if (!oled_.begin(OLED_I2C_ADDR, true)) return false;
#endif
#if DISP_ROTATION == 3 || DISP_ROTATION == 2
        oled_.setRotation(2);
#endif
        oled_.clearDisplay();
        oled_.display();
        return true;
    }
    void render(const RenderFn& fn) override {
        oled_.clearDisplay();
        fn(oled_);
        oled_.display();
    }
    bool isColor() const override { return false; }

private:
    OledDriver oled_;
};
#endif

// ======================================================================= TFT
#if defined(DISPLAY_ST7735) || defined(DISPLAY_ST7789) || defined(DISPLAY_ILI9341)
#if defined(DISPLAY_ILI9341)
#include <Adafruit_ILI9341.h>
#else
#include <Adafruit_ST7735.h>
#include <Adafruit_ST7789.h>
#endif

// Полоса кадра: Adafruit_GFX «думает», что рисует на полном экране (DISP_W×DISP_H),
// а пиксели попадают в буфер высотой DISP_BAND_H со смещением y0.
class BandCanvas : public Adafruit_GFX {
public:
    BandCanvas() : Adafruit_GFX(DISP_W, DISP_H) {}
    bool alloc() {
        buf_ = (uint16_t*)malloc((size_t)DISP_W * DISP_BAND_H * 2);
        return buf_ != nullptr;
    }
    void setBand(int16_t y0, int16_t h) { y0_ = y0; h_ = h; }
    uint16_t* buffer() { return buf_; }

    void drawPixel(int16_t x, int16_t y, uint16_t c) override {
        y -= y0_;
        if (x < 0 || x >= DISP_W || y < 0 || y >= h_) return;
        buf_[y * DISP_W + x] = c;
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
        y -= y0_;
        if (y < 0 || y >= h_ || w <= 0) return;
        if (x < 0) { w += x; x = 0; }
        if (x + w > DISP_W) w = DISP_W - x;
        if (w <= 0) return;
        uint16_t* p = buf_ + y * DISP_W + x;
        while (w--) *p++ = c;
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
        if (x < 0 || x >= DISP_W || h <= 0) return;
        y -= y0_;
        if (y < 0) { h += y; y = 0; }
        if (y + h > h_) h = h_ - y;
        if (h <= 0) return;
        uint16_t* p = buf_ + y * DISP_W + x;
        while (h--) { *p = c; p += DISP_W; }
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
        for (int16_t j = 0; j < h; ++j) drawFastHLine(x, y + j, w, c);
    }
    void fillScreen(uint16_t c) override {
        const uint32_t n = (uint32_t)DISP_W * h_;
        for (uint32_t k = 0; k < n; ++k) buf_[k] = c;
    }

private:
    uint16_t* buf_ = nullptr;
    int16_t y0_ = 0, h_ = DISP_BAND_H;
};

class TftDisplay : public Display {
public:
#if defined(DISPLAY_ILI9341)
    TftDisplay() : tft_(&SPI, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST) {}
#elif defined(DISPLAY_ST7789)
    TftDisplay() : tft_(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST) {}
#else
    TftDisplay() : tft_(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST) {}
#endif

    bool begin() override {
#if defined(PIN_TFT_BL) && PIN_TFT_BL >= 0
        pinMode(PIN_TFT_BL, OUTPUT);
        digitalWrite(PIN_TFT_BL, HIGH);
#endif
#if PLATFORM_ESP32
        SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
#else
        SPI.setSCK(PIN_TFT_SCK);
        SPI.setTX(PIN_TFT_MOSI);
#endif
#if defined(DISPLAY_ILI9341)
        tft_.begin(DISP_SPI_HZ);
#elif defined(DISPLAY_ST7789)
        tft_.init(DISP_H, DISP_W);  // драйвер принимает размеры в портретной ориентации
        tft_.setSPISpeed(DISP_SPI_HZ);
#else
        tft_.initR(ST7735_TAB);
        tft_.setSPISpeed(DISP_SPI_HZ > 27000000 ? 27000000 : DISP_SPI_HZ);
#endif
        tft_.setRotation(DISP_ROTATION);
        tft_.fillScreen(0);
        return canvas_.alloc();
    }

    void render(const RenderFn& fn) override {
        for (int16_t y0 = 0; y0 < DISP_H; y0 += DISP_BAND_H) {
            const int16_t h = (DISP_H - y0) < DISP_BAND_H ? (DISP_H - y0) : DISP_BAND_H;
            canvas_.setBand(y0, h);
            canvas_.fillScreen(pal::BG);
            fn(canvas_);
            tft_.startWrite();
            tft_.setAddrWindow(0, y0, DISP_W, h);
            tft_.writePixels(canvas_.buffer(), (uint32_t)DISP_W * h, true, false);
            tft_.endWrite();
        }
    }
    bool isColor() const override { return true; }

private:
#if defined(DISPLAY_ILI9341)
    Adafruit_ILI9341 tft_;
#elif defined(DISPLAY_ST7789)
    Adafruit_ST7789 tft_;
#else
    Adafruit_ST7735 tft_;
#endif
    BandCanvas canvas_;
};
#endif

Display* createDisplay() {
#if defined(DISPLAY_SSD1306) || defined(DISPLAY_SH1106)
    static OledDisplay d;
    return &d;
#elif defined(DISPLAY_ST7735) || defined(DISPLAY_ST7789) || defined(DISPLAY_ILI9341)
    static TftDisplay d;
    return &d;
#else
    return nullptr;
#endif
}
