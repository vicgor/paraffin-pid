#pragma once
// Экранный интерфейс: главный экран, меню, редактирование, инфо-экраны.
// Управление: вращение — уставка (на главном) / навигация; клик — меню / выбор;
// удержание 1.2 с — Пуск/Стоп (на главном), сброс аварии, «назад».
#include <Arduino.h>

#include "Controller.h"
#include "display/display.h"
#include "input.h"

class Ui {
public:
    void begin(Display* d, InputDevice* in);
    void tick(uint32_t nowMs);
    void toast(const char* msg, uint32_t ms = 1500);

private:
    enum class Screen : uint8_t { Main, Menu, Edit, Sensors, Network };

    void handleInput(uint32_t now);
    void onMainRotate(int32_t d, uint32_t now);
    void onMenuClick(uint32_t now);
    void draw(Adafruit_GFX& g);
    void drawMain(Adafruit_GFX& g);
    void drawMainCompact(Adafruit_GFX& g);
    void drawMainLarge(Adafruit_GFX& g);
    void drawGraph(Adafruit_GFX& g, int16_t x, int16_t y, int16_t w, int16_t h);
    void drawMenu(Adafruit_GFX& g);
    void drawEdit(Adafruit_GFX& g);
    void drawInfo(Adafruit_GFX& g, bool network);
    void drawToast(Adafruit_GFX& g);
    void menuValue(uint8_t item, char* out, size_t n);

    Display* d_ = nullptr;
    InputDevice* in_ = nullptr;
    Screen screen_ = Screen::Main;
    uint8_t sel_ = 0, top_ = 0;
    float editVal_ = 0;
    uint32_t lastInputMs_ = 0, lastDrawMs_ = 0, setFlashUntil_ = 0;
    char toast_[32] = "";
    uint32_t toastUntil_ = 0;
    bool dirty_ = true;
    // снимок состояния на кадр (render вызывает draw несколько раз — по полосам)
    ctl::ControllerStatus st_;
    ctl::ControlSettings cs_;
    bool heaterOn_ = false;
    char net_[40] = "";
};
