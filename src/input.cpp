#include "input.h"

#include "hal.h"
#include "pins.h"

namespace {
volatile int32_t g_encCount = 0;
volatile uint8_t g_encState = 0;
int32_t g_encTaken = 0;

// Таблица переходов квадратурного сигнала: устойчива к дребезгу
const int8_t kQuad[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

#if PIN_ENC_A >= 0 && PIN_ENC_B >= 0
void ISR_ATTR encIsr() {
    const uint8_t s = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    g_encState = ((g_encState << 2) | s) & 0x0F;
    g_encCount += kQuad[g_encState];
}
#endif
}  // namespace

void InputDevice::begin() {
    buttons_ = {};
    backPending_ = false;
#if PIN_ENC_A >= 0 && PIN_ENC_B >= 0
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    g_encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encIsr, CHANGE);
#endif
#if PIN_ENC_BTN >= 0
    pinMode(PIN_ENC_BTN, INPUT_PULLUP);
#endif
#if PIN_BTN_OK >= 0
    pinMode(PIN_BTN_OK, INPUT_PULLUP);
#endif
#if PIN_BTN_BACK >= 0
    pinMode(PIN_BTN_BACK, INPUT_PULLUP);
#endif
#if defined(PIN_BTN_UP) && defined(PIN_BTN_DOWN)
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
#endif
}

int32_t InputDevice::takeDelta() {
    noInterrupts();
    const int32_t c = g_encCount;
    interrupts();
    const int32_t detents = (c - g_encTaken) / ENC_STEPS_PER_DETENT;
    g_encTaken += detents * ENC_STEPS_PER_DETENT;
    int32_t d = detents;
#if defined(ENC_REVERSE)
    d = -d;
#endif
#if defined(PIN_BTN_UP) && defined(PIN_BTN_DOWN)
    d += btnDelta_;
    btnDelta_ = 0;
#endif
    return d;
}

ButtonEvent InputDevice::poll(uint32_t nowMs) {
#if defined(PIN_BTN_UP) && defined(PIN_BTN_DOWN)
    // Кнопки вверх/вниз с автоповтором
    auto rep = [&](int pin, uint32_t& t0, int dir) {
        if (digitalRead(pin) == LOW) {
            if (t0 == 0) {
                t0 = nowMs;
                btnDelta_ += dir;
            } else if (nowMs - t0 > 500) {
                btnDelta_ += dir;
                t0 = nowMs - 400;
            }
        } else {
            t0 = 0;
        }
    };
    rep(PIN_BTN_UP, repUp_, +1);
    rep(PIN_BTN_DOWN, repDn_, -1);
#endif
    uint8_t mask = 0;
#if PIN_ENC_BTN >= 0
    if (digitalRead(PIN_ENC_BTN) == LOW) mask |= uiinput::Buttons::kEncoder;
#endif
#if PIN_BTN_OK >= 0
    if (digitalRead(PIN_BTN_OK) == LOW) mask |= uiinput::Buttons::kConfirm;
#endif
#if PIN_BTN_BACK >= 0
    if (digitalRead(PIN_BTN_BACK) == LOW) mask |= uiinput::Buttons::kBack;
#endif
    const ButtonEvent event = buttons_.poll(mask, nowMs);
    // BACK доставляется отдельно: старый обработчик OK не должен принять его за запуск.
    if (event == ButtonEvent::Back) {
        backPending_ = true;
        return ButtonEvent::None;
    }
    return event;
}
