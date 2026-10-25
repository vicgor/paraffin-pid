#pragma once
// Медленный ШИМ (time-proportioning) для твердотельного реле с переходом через ноль.
// Окно 2 с при 50 Гц = 100 полупериодов → разрешение 1 %.
#include <Arduino.h>

class SsrOutput {
public:
    // dryRun: логика ШИМ работает (isOn() для симулятора), но пин всегда
    // в неактивном уровне — sim-прошивка не может включить настоящий ТЭН.
    void begin(int pin, bool activeHigh, bool dryRun = false) {
        pin_ = pin;
        activeHigh_ = activeHigh;
        dryRun_ = dryRun;
        if (pin_ < 0) return;
        write(false);
        pinMode(pin_, OUTPUT);
        write(false);
    }

    void setWindow(uint16_t ms) { windowMs_ = ms < 200 ? 200 : ms; }

    // Новая скважность применяется со следующего окна; 0 — немедленно.
    void setDuty(float pct) {
        if (pct <= 0) {
            pending_ = 0;
            duty_ = 0;
            if (on_) write(false);
            return;
        }
        pending_ = pct > 100 ? 100 : pct;
    }

    void tick(uint32_t nowMs) {
        if (pin_ < 0) return;
        if (nowMs - winStart_ >= windowMs_) {
            winStart_ = nowMs;
            duty_ = pending_;
        }
        uint32_t onMs = (uint32_t)(duty_ * windowMs_ / 100.0f);
        if (onMs < kMinPulseMs) onMs = 0;                                  // короче полупериода — смысла нет
        if (onMs > 0 && windowMs_ - onMs < kMinPulseMs) onMs = windowMs_;  // почти 100 % → 100 %
        const bool want = (nowMs - winStart_) < onMs;
        if (want != on_) write(want);
    }

    void forceOff() {
        pending_ = duty_ = 0;
        write(false);
    }
    bool isOn() const { return on_; }
    float duty() const { return duty_; }
    bool dryRun() const { return dryRun_; }

private:
    static constexpr uint32_t kMinPulseMs = 20;
    void write(bool on) {
        on_ = on;
        const bool level = on && !dryRun_;  // в dry run пин никогда не активен
        if (pin_ >= 0) digitalWrite(pin_, (level == activeHigh_) ? HIGH : LOW);
    }

    int pin_ = -1;
    bool activeHigh_ = true;
    bool dryRun_ = false;
    uint16_t windowMs_ = 2000;
    uint32_t winStart_ = 0;
    float duty_ = 0, pending_ = 0;
    bool on_ = false;
};
