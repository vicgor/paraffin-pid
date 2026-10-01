#pragma once
// Энкодер EC11 с кнопкой (по прерываниям) или три кнопки UP/DOWN/OK.
// Для кнопок задайте -DPIN_BTN_UP=.. -DPIN_BTN_DOWN=.. (OK = PIN_ENC_BTN).
#include <Arduino.h>

enum class ButtonEvent : uint8_t { None, Click, Long };

class InputDevice {
public:
    void begin();
    int32_t takeDelta();                 // щелчков энкодера с прошлого вызова (±)
    ButtonEvent poll(uint32_t nowMs);    // события кнопки OK
    bool buttonHeld() const { return pressed_; }

private:
    bool pressed_ = false;
    bool longSent_ = false;
    uint32_t pressMs_ = 0, changeMs_ = 0;
    bool raw_ = false;
#if defined(PIN_BTN_UP) && defined(PIN_BTN_DOWN)
    uint32_t repUp_ = 0, repDn_ = 0;
    int32_t btnDelta_ = 0;
#endif
};
