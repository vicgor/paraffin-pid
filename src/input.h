#pragma once
// Энкодер EC11 или UP/DOWN/OK; CON дублирует OK, BACK имеет отдельное событие.
#include <Arduino.h>
#include "ui_buttons.h"

using ButtonEvent = uiinput::Event;

class InputDevice {
public:
    void begin();
    int32_t takeDelta();
    ButtonEvent poll(uint32_t nowMs);
    bool buttonHeld() const { return buttons_.held(); }
    bool takeBack() {
        const bool pending = backPending_;
        backPending_ = false;
        return pending;
    }

private:
    uiinput::Buttons buttons_;
    bool backPending_ = false;
#if defined(PIN_BTN_UP) && defined(PIN_BTN_DOWN)
    uint32_t repUp_ = 0, repDn_ = 0;
    int32_t btnDelta_ = 0;
#endif
};
